//! Queued messages (PROTOCOL §6d, proto 0.17): a quick reply or a dictation the user asked for
//! while the agent was working is held here, in memory, and typed the next time the session can
//! take input. One delivery task serves the whole daemon, so two linked devices never type it twice.

use std::collections::{HashMap, HashSet};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use agentlink_proto::{InputSub, SessState, Session};
use serde_json::json;
use tokio::sync::broadcast::error::RecvError;

use crate::device::Shared;
use crate::registry::{clip, Route, Update};

/// How long the terminal gets to settle after the session can take input.
pub const SETTLE: Duration = Duration::from_secs(2);
/// Longest `qd` the device is sent.
const PREVIEW_CHARS: usize = 48;

struct Queued {
    text: String,
    /// Where the text came from (a reply index or a recording), for the audit line: never the text.
    source: String,
}

#[derive(Default)]
pub struct Queue {
    msgs: Mutex<HashMap<u64, Queued>>,
    /// Sessions with a delivery in flight.
    busy: Mutex<HashSet<u64>>,
    /// The (state, sub) a session was in when delivery failed: no retry until it changes.
    failed: Mutex<HashMap<u64, Phase>>,
}

type Phase = (Option<SessState>, Option<InputSub>);

impl Queue {
    /// Holds `text` for `sid`, replacing an earlier one.
    pub fn put(&self, sid: u64, text: String, source: String) {
        self.msgs
            .lock()
            .unwrap()
            .insert(sid, Queued { text, source });
    }

    pub fn remove(&self, sid: u64) -> bool {
        self.msgs.lock().unwrap().remove(&sid).is_some()
    }

    pub fn is_empty_for(&self, sid: u64) -> bool {
        !self.msgs.lock().unwrap().contains_key(&sid)
    }

    fn sids(&self) -> Vec<u64> {
        self.msgs.lock().unwrap().keys().copied().collect()
    }

    fn take(&self, sid: u64) -> Option<Queued> {
        self.msgs.lock().unwrap().remove(&sid)
    }

    /// Puts a message back after a failed delivery, unless a newer one was queued meanwhile.
    fn restore(&self, sid: u64, q: Queued) {
        self.msgs.lock().unwrap().entry(sid).or_insert(q);
    }

    fn begin(&self, sid: u64) -> bool {
        self.busy.lock().unwrap().insert(sid)
    }

    fn end(&self, sid: u64) {
        self.busy.lock().unwrap().remove(&sid);
    }
}

/// The start of `text` for the device: at most 48 chars on one line, cut with `…`.
pub fn preview(text: &str) -> String {
    let one_line: String = text
        .chars()
        .map(|c| if c.is_control() { ' ' } else { c })
        .collect();
    clip(&one_line, PREVIEW_CHARS)
}

/// Whether the session can be typed into now (PROTOCOL §6d).
pub fn can_take_input(session: &Session, request_open: bool) -> bool {
    !matches!(session.st, Some(SessState::Start | SessState::Work))
        && crate::typing::refusal(session, request_open).is_none()
}

pub fn spawn_delivery(shared: Arc<Shared>, settle: Duration) {
    let (_, mut rx) = shared.registry.subscribe();
    tokio::spawn(async move {
        loop {
            match rx.recv().await {
                Ok(Update::Gone(sid)) => {
                    shared.queue.remove(sid);
                }
                // a state change (even back to the same state) earns a new attempt
                Ok(Update::Upsert(s)) if s.st.is_some() => {
                    shared.queue.failed.lock().unwrap().remove(&s.sid);
                }
                Ok(_) | Err(RecvError::Lagged(_)) => {}
                Err(RecvError::Closed) => return,
            }
            for sid in shared.queue.sids() {
                if ready(&shared, sid) && shared.queue.begin(sid) {
                    let shared = shared.clone();
                    tokio::spawn(async move {
                        tokio::time::sleep(settle).await;
                        deliver(&shared, sid).await;
                        shared.queue.end(sid);
                    });
                }
            }
        }
    });
}

fn ready(shared: &Shared, sid: u64) -> bool {
    match shared.registry.route(sid) {
        Route::Ready(s, _) => {
            can_take_input(&s, shared.broker.open_for(sid))
                && shared.queue.failed.lock().unwrap().get(&sid) != Some(&(s.st, s.sub))
        }
        Route::Unknown => {
            shared.queue.remove(sid);
            false
        }
        Route::Unsupported => false,
    }
}

/// Types the queued message of `sid` if the session can take it now. A refusal or a failure keeps
/// the message for the session's next change.
async fn deliver(shared: &Shared, sid: u64) {
    let (ctx, phase) = match shared.registry.route(sid) {
        Route::Ready(s, ctx) if can_take_input(&s, shared.broker.open_for(sid)) => {
            (ctx, (s.st, s.sub))
        }
        Route::Unknown => {
            shared.queue.remove(sid);
            return;
        }
        _ => return,
    };
    let Some(q) = shared.queue.take(sid) else {
        return;
    };
    let term = shared.term.clone();
    let text = q.text.clone();
    let res =
        tokio::task::spawn_blocking(move || crate::typing::type_reply(term.as_ref(), &ctx, &text))
            .await
            .unwrap_or_else(|e| Err(e.to_string()));
    shared.trace.audit(
        "queue_deliver",
        Some(sid),
        None,
        json!({"source": q.source, "ok": res.is_ok()}),
    );
    match res {
        Ok(()) => {
            shared.queue.failed.lock().unwrap().remove(&sid);
            if shared.queue.is_empty_for(sid) {
                shared.registry.set_qd(sid, "");
            }
        }
        Err(m) => {
            tracing::warn!("sid {sid}: queued message not typed, kept for later: {m}");
            shared.queue.restore(sid, q);
            shared.queue.failed.lock().unwrap().insert(sid, phase);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preview_is_one_line_of_at_most_48_chars() {
        assert_eq!(preview("fix it\nthen\r\nrun"), "fix it then  run");
        let long = "é".repeat(60);
        let p = preview(&long);
        assert_eq!(p.chars().count(), 48);
        assert!(p.ends_with('…'));
        assert_eq!(preview(&"a".repeat(48)), "a".repeat(48));
    }
}
