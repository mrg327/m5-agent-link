//! Approval broker: one pending hook response per request id (DESIGN §6a, UI-V2 §7).
//!
//! A blocking agent hook (Claude Code `PermissionRequest`, or `PreToolUse` for
//! `AskUserQuestion`) opens a request here and waits on the returned channel. Devices see
//! it as an `attention` with the agent's own choices (`opts`, `grant_mask`); their
//! `approve` / `deny` / `reply` commands land in [`Broker::answer`]. **First answer wins**:
//! the request is removed the moment it is decided, cancelled or expired, so a late command
//! gets `stale`, and a hook whose channel closes without a decision reports *no* decision,
//! never an approval.

use crate::registry::{clip, now_unix, Registry, Update};
use agentlink_proto::{Action, AttKind, Attention, Clear, ClearWhy, ResultErr};
use serde::Serialize;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::oneshot;

/// Longest full text a device can fetch with `detail` (ANSWER-WINDOW.md §4): 4 KiB, about twenty
/// pages of the watch's reader. A longer request stays "Too long to approve here".
pub const DETAIL_CAP: usize = 4096;
/// Bytes of text per `detail` chunk: one chunk is a fraction of the watch's 4096-byte message limit.
pub const DETAIL_CHUNK: usize = 1024;

/// What the device answered.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Decision {
    Approve,
    Deny,
    /// Index into the request's `opts`.
    Reply(usize),
}

/// What one of the request's options means to the agent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Meaning {
    Allow,
    /// Allow and remember (Claude Code: apply `permission_suggestions`).
    AllowGrant,
    Deny,
    /// A question's answer: the option's full label.
    Answer(String),
}

/// What the hook is told; it turns this into the agent's own hook-output JSON.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Outcome {
    /// `allow` | `deny` | `none` (no decision: let the agent prompt as usual).
    pub decision: &'static str,
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    pub grant: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub answer: Option<String>,
    /// Index of the option the decision stands for (Approve: the first allow, Deny: the first
    /// deny), so a typed answer knows which key to press. Not sent to hooks.
    #[serde(skip)]
    pub choice: Option<usize>,
}

impl Outcome {
    pub const NONE: Outcome = Outcome {
        decision: "none",
        grant: false,
        answer: None,
        choice: None,
    };

    fn of(
        decision: &'static str,
        grant: bool,
        answer: Option<String>,
        choice: Option<usize>,
    ) -> Self {
        Outcome {
            decision,
            grant,
            answer,
            choice,
        }
    }
}

/// A request as the hook describes it, before it has a `rid`.
#[derive(Debug, Clone)]
pub struct Request {
    pub sid: u64,
    /// Source session key (`agent:session_id`), for cancelling by session.
    pub key: String,
    pub tool: Option<String>,
    pub kind: AttKind,
    pub title: Option<String>,
    pub text: Option<String>,
    /// `opts` and their meanings, same length; empty = the device synthesises Approve / Deny.
    pub opts: Vec<(String, Meaning)>,
    /// The answer window (window.rs); a notice's is how long it may stay up.
    pub ttl: Duration,
    /// Nothing can answer it but the terminal: the device only offers Focus.
    pub focus_only: bool,
    /// Takes a dictated answer (`answer_text`): the device offers Dictate.
    pub dictate: bool,
    /// Shown only: the hook is released at once (the terminal shows the prompt now) and the card
    /// stays until the session moves on, e.g. a question the wheel cannot answer.
    pub notice: bool,
}

struct Pending {
    att: Attention,
    key: String,
    tool: Option<String>,
    meanings: Vec<Meaning>,
    tx: oneshot::Sender<Outcome>,
    /// The full text as the watch's reader shows it (`detail_text`); `None` past [`DETAIL_CAP`].
    full: Option<String>,
    /// When the answer window ends; `None` for a notice (no countdown).
    deadline: Option<Instant>,
}

#[derive(Default)]
struct Inner {
    next_rid: u64,
    pending: HashMap<u64, Pending>,
}

pub struct Broker {
    inner: Mutex<Inner>,
    registry: Arc<Registry>,
    /// Devices linked right now (device.rs holds a [`Linked`] per handshaken link).
    linked: Arc<AtomicUsize>,
}

/// Counts one linked device while it lives.
pub struct Linked(Arc<AtomicUsize>);

impl Drop for Linked {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::Relaxed);
    }
}

/// Text that is safe to show on a watch *in full* (the `detail` reader): every character the watch
/// cannot draw is spelled out instead of vanishing (ANSWER-WINDOW.md §4). Line breaks stay; other
/// control characters become caret notation (`^[` for Esc, `^I` for a tab); characters outside the
/// watch's glyph set, and the invisible or look-alike ones inside it (no-break space, soft hyphen),
/// become `\u{…}`. Nothing is collapsed or dropped.
pub fn detail_text(s: &str) -> String {
    let s = s.replace("\r\n", "\n");
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '\n' => out.push('\n'),
            '\r' => out.push('\n'),
            c if (c as u32) < 0x20 => {
                out.push('^');
                out.push(char::from(b'@' + c as u8));
            }
            '\u{7f}' => out.push_str("^?"),
            '\u{20}'..='\u{7e}' | '\u{a1}'..='\u{ac}' | '\u{ae}'..='\u{ff}' => out.push(c),
            '\u{2013}' | '\u{2014}' | '\u{2018}' | '\u{2019}' | '\u{201c}' | '\u{201d}'
            | '\u{2022}' | '\u{2026}' => out.push(c),
            c => out.push_str(&format!("\\u{{{:x}}}", c as u32)),
        }
    }
    out
}

/// Text that is safe to show on a watch: control characters stripped, whitespace collapsed.
pub fn tidy(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut space = false;
    for c in s.chars() {
        if c.is_control() && !c.is_whitespace() {
            continue; /* dropped outright: a bell or escape is not a word break */
        }
        if c.is_whitespace() {
            if !space && !out.is_empty() {
                out.push(' ');
            }
            space = true;
        } else {
            out.push(c);
            space = false;
        }
    }
    out.trim_end().to_owned()
}

/// First request id of this daemon run. Random, not 1: a watch can still show a card from an
/// earlier run, and its answer names only the rid, so a reused rid could approve a different
/// request (issue 20261109-1173). Kept below 2^31 so a run has room before the device's
/// 32-bit rid limit.
fn first_rid() -> u64 {
    use std::hash::{BuildHasher, Hasher};
    let r = std::collections::hash_map::RandomState::new()
        .build_hasher()
        .finish();
    1 + (r % (1 << 31))
}

impl Broker {
    pub fn new(registry: Arc<Registry>) -> Self {
        Broker {
            inner: Mutex::new(Inner {
                next_rid: first_rid(),
                ..Default::default()
            }),
            registry,
            linked: Arc::new(AtomicUsize::new(0)),
        }
    }

    /// A device finished its handshake; it counts as linked until the guard drops.
    pub fn link(&self) -> Linked {
        self.linked.fetch_add(1, Ordering::Relaxed);
        Linked(self.linked.clone())
    }

    /// Devices linked right now: with none, nobody can answer (window.rs).
    pub fn devices(&self) -> usize {
        self.linked.load(Ordering::Relaxed)
    }

    /// Registers the request, tells every device, and returns the channel the hook waits on.
    /// The channel yields `Err` when the request ends without a device decision.
    pub fn open(&self, req: Request) -> (u64, oneshot::Receiver<Outcome>) {
        let (tx, rx) = oneshot::channel();
        let mut acts = match req.kind {
            AttKind::Approval if !req.focus_only => {
                vec![Action::Approve, Action::Deny, Action::Focus]
            }
            _ => vec![Action::Focus],
        };
        if !req.opts.is_empty() && !req.focus_only {
            acts.push(Action::Reply);
        }
        if req.dictate {
            acts.push(Action::Dictate);
        }
        let grant_mask = req
            .opts
            .iter()
            .enumerate()
            .filter(|(_, (_, m))| matches!(m, Meaning::Allow | Meaning::AllowGrant))
            .fold(0u64, |mask, (i, _)| mask | (1 << i));
        let mut g = self.inner.lock().unwrap();
        let rid = g.next_rid;
        g.next_rid = if rid >= u64::from(u32::MAX) {
            1
        } else {
            rid + 1
        };
        let text = req.text.as_deref().map(|t| clip(&tidy(t), 160));
        let full = req.text.as_deref().map(detail_text);
        // `len` says the preview is not the whole text (cut, or tidied): the device fetches it
        let len = full
            .as_ref()
            .filter(|f| text.as_ref() != Some(*f))
            .map(|f| f.len() as u64);
        let win_ms = (!req.notice).then_some(req.ttl.as_millis() as u64);
        let att = Attention {
            rid,
            sid: req.sid,
            kind: req.kind,
            title: req.title.map(|t| clip(&tidy(&t), 40)),
            text,
            opts: (!req.opts.is_empty() && !req.focus_only)
                .then(|| req.opts.iter().map(|(o, _)| clip(&tidy(o), 24)).collect()),
            acts,
            exp: Some(now_unix() + req.ttl.as_secs().max(1)),
            grant_mask: (!req.opts.is_empty() && !req.focus_only).then_some(grant_mask),
            left: win_ms,
            win: win_ms,
            len,
        };
        g.pending.insert(
            rid,
            Pending {
                att: att.clone(),
                key: req.key,
                tool: req.tool,
                meanings: req.opts.into_iter().map(|(_, m)| m).collect(),
                tx,
                full: full.filter(|f| f.len() <= DETAIL_CAP),
                deadline: (!req.notice).then(|| Instant::now() + req.ttl),
            },
        );
        drop(g);
        self.registry.publish(Update::Attention(att));
        (rid, rx)
    }

    /// A device's answer. Unknown or already-settled `rid` → `stale`; a reply index outside
    /// the options → `denied`. On success the hook is released and devices get a `clear`.
    pub fn answer(
        &self,
        rid: u64,
        decision: Decision,
    ) -> Result<Outcome, (ResultErr, &'static str)> {
        let g = self.inner.lock().unwrap();
        let Some(p) = g.pending.get(&rid) else {
            return Err((ResultErr::Stale, "no pending request for rid"));
        };
        if p.att.acts == [Action::Focus] {
            return Err((ResultErr::Denied, "answer it in the terminal"));
        }
        let first = |want: fn(&Meaning) -> bool| p.meanings.iter().position(want);
        let outcome = match decision {
            Decision::Approve if p.att.kind == AttKind::Approval => {
                Outcome::of("allow", false, None, first(|m| matches!(m, Meaning::Allow)))
            }
            Decision::Deny if p.att.kind == AttKind::Approval => {
                Outcome::of("deny", false, None, first(|m| matches!(m, Meaning::Deny)))
            }
            Decision::Reply(i) => match p.meanings.get(i) {
                Some(Meaning::Allow) => Outcome::of("allow", false, None, Some(i)),
                Some(Meaning::AllowGrant) => Outcome::of("allow", true, None, Some(i)),
                Some(Meaning::Deny) => Outcome::of("deny", false, None, Some(i)),
                Some(Meaning::Answer(a)) => Outcome::of("allow", false, Some(a.clone()), Some(i)),
                None => return Err((ResultErr::Denied, "no such option")),
            },
            _ => return Err((ResultErr::Denied, "not a permission request")),
        };
        self.settle(g, rid, outcome)
    }

    /// Whether `rid` is open and takes a dictated answer.
    pub fn takes_dictation(&self, rid: u64) -> bool {
        let g = self.inner.lock().unwrap();
        g.pending
            .get(&rid)
            .is_some_and(|p| p.att.acts.contains(&Action::Dictate))
    }

    /// A device's dictated answer to a card that takes one (`Action::Dictate`).
    pub fn answer_text(
        &self,
        rid: u64,
        text: String,
    ) -> Result<Outcome, (ResultErr, &'static str)> {
        let g = self.inner.lock().unwrap();
        let Some(p) = g.pending.get(&rid) else {
            return Err((ResultErr::Stale, "no pending request for rid"));
        };
        if !p.att.acts.contains(&Action::Dictate) {
            return Err((ResultErr::Denied, "this card takes no dictation"));
        }
        self.settle(g, rid, Outcome::of("allow", false, Some(text), None))
    }

    fn settle(
        &self,
        mut g: std::sync::MutexGuard<'_, Inner>,
        rid: u64,
        outcome: Outcome,
    ) -> Result<Outcome, (ResultErr, &'static str)> {
        let p = g.pending.remove(&rid).unwrap();
        drop(g);
        // The hook may already be gone; the clear still goes out, but the device is told.
        let delivered = p.tx.send(outcome.clone()).is_ok();
        self.registry.publish(Update::Clear(Clear {
            rid,
            why: ClearWhy::AnsweredDev,
        }));
        if delivered {
            Ok(outcome)
        } else {
            Err((ResultErr::Stale, "hook gone"))
        }
    }

    /// Ends a request without a device decision (timeout, terminal answer, hook gone).
    /// True when `decision` on `rid` would grant permission (Approve, or a reply whose option
    /// means allow): the caller must then insist on a full hold, whichever op carried it.
    pub fn grants(&self, rid: u64, decision: &Decision) -> bool {
        let g = self.inner.lock().unwrap();
        let Some(p) = g.pending.get(&rid) else {
            return false;
        };
        match decision {
            Decision::Approve => p.att.kind == AttKind::Approval,
            Decision::Deny => false,
            Decision::Reply(i) => matches!(
                p.meanings.get(*i),
                Some(Meaning::Allow) | Some(Meaning::AllowGrant)
            ),
        }
    }

    pub fn cancel(&self, rid: u64, why: ClearWhy) -> bool {
        let removed = self.inner.lock().unwrap().pending.remove(&rid);
        if removed.is_some() {
            self.registry.publish(Update::Clear(Clear { rid, why }));
        }
        removed.is_some()
    }

    /// The terminal moved on (a later event from the same session, optionally for the same
    /// tool): whatever was pending there was answered on the PC. A tool's end also answers
    /// cards that name no tool (a prompt read off the screen), but not another tool's card.
    pub fn cancel_session(&self, key: &str, tool: Option<&str>) {
        let rids: Vec<u64> = self
            .inner
            .lock()
            .unwrap()
            .pending
            .iter()
            .filter(|(_, p)| {
                p.key == key && tool.is_none_or(|t| p.tool.as_deref().is_none_or(|pt| pt == t))
            })
            .map(|(rid, _)| *rid)
            .collect();
        for rid in rids {
            self.cancel(rid, ClearWhy::AnsweredPc);
        }
    }

    /// Whether `sid` has a request waiting for an answer (typing into it is refused then).
    pub fn open_for(&self, sid: u64) -> bool {
        let g = self.inner.lock().unwrap();
        g.pending.values().any(|p| p.att.sid == sid)
    }

    /// Open requests, for a device that connects while they are pending: `left` is what remains of
    /// each window now, so a watch that reconnects mid-window counts down from the right place.
    pub fn pending(&self) -> Vec<Attention> {
        let g = self.inner.lock().unwrap();
        let now = Instant::now();
        let mut v: Vec<Attention> = g
            .pending
            .values()
            .map(|p| {
                let mut a = p.att.clone();
                if let Some(d) = p.deadline {
                    a.left = Some(d.saturating_duration_since(now).as_millis() as u64);
                }
                a
            })
            .collect();
        v.sort_by_key(|a| a.rid);
        v
    }

    /// The full text of open request `rid`, for a device's `detail` (ANSWER-WINDOW.md §4): `stale`
    /// when it is no longer open, `denied` past [`DETAIL_CAP`] or when it has no text.
    pub fn detail(&self, rid: u64) -> Result<(u64, String), (ResultErr, &'static str)> {
        let g = self.inner.lock().unwrap();
        let Some(p) = g.pending.get(&rid) else {
            return Err((ResultErr::Stale, "no pending request for rid"));
        };
        match (&p.full, p.att.len) {
            (Some(f), _) => Ok((p.att.sid, f.clone())),
            (None, Some(_)) => Err((ResultErr::Denied, "too long for the watch")),
            (None, None) => Err((ResultErr::Denied, "no text")),
        }
    }

    /// The device's Open on PC on request `rid` (proto 0.11): the hook is released with no decision at
    /// once, so the terminal shows its own prompt now instead of at the end of the window. Never an
    /// answer: the same as the window running out. False when `rid` is not open.
    pub fn hand_back(&self, rid: u64, sid: Option<u64>) -> bool {
        let mine = {
            let g = self.inner.lock().unwrap();
            g.pending
                .get(&rid)
                .is_some_and(|p| sid.is_none_or(|s| s == p.att.sid))
        };
        mine && self.cancel(rid, ClearWhy::HandedBack)
    }
}

/// Builds the request a blocking hook line describes, or `None` when the watch cannot answer
/// it (then the hook gets `none` at once and the agent prompts as usual).
/// How long a notice card (a question only the terminal can answer) may stay up if the session
/// never moves on; normally the next hook event clears it.
pub const NOTICE_TTL: Duration = Duration::from_secs(600);

pub fn request_of(
    sid: u64,
    key: String,
    agent: &str,
    event: &str,
    payload: &Value,
    ttl: Duration,
) -> Option<Request> {
    if agent != "claude" {
        return None;
    }
    let tool = payload["tool_name"].as_str().map(str::to_owned);
    let input = &payload["tool_input"];
    match event.to_ascii_lowercase().as_str() {
        // Claude Code also asks permission to *show* a question (AskUserQuestion) once the
        // PreToolUse hook gave no answer. That is not a tool to approve: the question was already
        // offered on the watch, so no card and no wait; the terminal shows the question at once.
        "permissionrequest" if tool.as_deref() == Some("AskUserQuestion") => None,
        "permissionrequest" => {
            let tool_name = tool.as_deref().unwrap_or("Tool");
            let (title, text) = describe_tool(tool_name, input);
            let has_suggestions = payload["permission_suggestions"]
                .as_array()
                .is_some_and(|a| !a.is_empty());
            let opts = if has_suggestions {
                vec![
                    ("Yes".into(), Meaning::Allow),
                    ("Yes, don't ask again".into(), Meaning::AllowGrant),
                    ("No".into(), Meaning::Deny),
                ]
            } else {
                Vec::new()
            };
            Some(Request {
                sid,
                key,
                tool,
                kind: AttKind::Approval,
                title: Some(title),
                text,
                opts,
                ttl,
                focus_only: false,
                dictate: false,
                notice: false,
            })
        }
        "pretooluse" if tool.as_deref() == Some("AskUserQuestion") => {
            let qs = input["questions"].as_array()?;
            // One question with single-select options is what a wheel can answer. Anything else
            // (several questions, multi-select, no options) is shown as a notice: the question
            // and Focus, while the terminal shows it at once.
            let notice = |q: &Value| {
                let header = q["header"]
                    .as_str()
                    .filter(|h| !h.is_empty())
                    .unwrap_or("Question");
                let multi = q["multiSelect"].as_bool() == Some(true);
                let text = q["question"].as_str().map(|t| {
                    format!(
                        "{t} ({})",
                        if multi {
                            "pick on the PC"
                        } else {
                            "answer on the PC"
                        }
                    )
                });
                Request {
                    sid,
                    key: key.clone(),
                    tool: tool.clone(),
                    kind: AttKind::Question,
                    title: Some(if qs.len() > 1 {
                        format!("{} questions", qs.len())
                    } else {
                        header.to_owned()
                    }),
                    text,
                    opts: Vec::new(),
                    ttl: NOTICE_TTL,
                    focus_only: true,
                    dictate: false,
                    notice: true,
                }
            };
            let [q] = qs.as_slice() else {
                return qs.first().map(notice);
            };
            if q["multiSelect"].as_bool() == Some(true) {
                return Some(notice(q));
            }
            let opts: Vec<(String, Meaning)> = q["options"]
                .as_array()
                .map(|a| a.as_slice())
                .unwrap_or(&[])
                .iter()
                .filter_map(|o| o["label"].as_str())
                .take(4)
                .map(|l| (l.to_owned(), Meaning::Answer(l.to_owned())))
                .collect();
            if opts.is_empty() {
                return Some(notice(q));
            }
            Some(Request {
                sid,
                key,
                tool,
                kind: AttKind::Question,
                title: Some(
                    q["header"]
                        .as_str()
                        .filter(|h| !h.is_empty())
                        .unwrap_or("Question")
                        .to_owned(),
                ),
                text: q["question"].as_str().map(str::to_owned),
                opts,
                ttl,
                focus_only: false,
                dictate: false,
                notice: false,
            })
        }
        _ => None,
    }
}

/// A watch-sized description of a tool call: what kind of thing, and the one detail that matters.
fn describe_tool(tool: &str, input: &Value) -> (String, Option<String>) {
    let s = |k: &str| input[k].as_str().map(str::to_owned);
    match tool {
        "Bash" => ("Run shell command".into(), s("command")),
        "Edit" | "MultiEdit" | "Write" | "NotebookEdit" => ("Edit file".into(), s("file_path")),
        "Read" => ("Read file".into(), s("file_path")),
        "WebFetch" => ("Fetch URL".into(), s("url")),
        "WebSearch" => ("Search the web".into(), s("query")),
        "Glob" | "Grep" => ("Search files".into(), s("pattern")),
        t => (
            t.to_owned(),
            input
                .as_object()
                .filter(|m| !m.is_empty())
                .map(|_| input.to_string()),
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::Trace;
    use serde_json::json;

    fn broker() -> (Arc<Broker>, tokio::sync::broadcast::Receiver<Update>) {
        let reg = Arc::new(Registry::new(Trace::null()));
        let (_, rx) = reg.subscribe();
        (Arc::new(Broker::new(reg)), rx)
    }

    fn perm(suggest: bool) -> Request {
        let payload = json!({"tool_name": "Bash", "tool_input": {"command": "npm test"},
                             "permission_suggestions": if suggest { json!([{"type":"addRules"}]) } else { json!([]) }});
        request_of(
            2,
            "claude:s".into(),
            "claude",
            "PermissionRequest",
            &payload,
            Duration::from_secs(15),
        )
        .unwrap()
    }

    fn next_update(rx: &mut tokio::sync::broadcast::Receiver<Update>) -> Update {
        rx.try_recv().expect("an update")
    }

    #[tokio::test]
    async fn a_new_daemon_run_does_not_reuse_old_rids() {
        // Issue 20261109-1173: the watch may still hold a card from the previous run, and its
        // answer names only the rid.
        let (old, _rx1) = broker();
        let (old_rid, _w1) = old.open(perm(false));
        let (new, _rx2) = broker();
        let (new_rid, _w2) = new.open(perm(false));
        assert_ne!(old_rid, new_rid);
        assert!(
            new_rid > 0 && new_rid <= u64::from(u32::MAX),
            "fits the device's u32 rid"
        );
        assert!(matches!(
            new.answer(old_rid, Decision::Approve),
            Err((ResultErr::Stale, _))
        ));
        assert_eq!(new.pending().len(), 1, "the new request is untouched");
    }

    #[test]
    fn showing_a_question_is_not_an_approval() {
        let payload = json!({"tool_name": "AskUserQuestion",
            "tool_input": {"questions": [{"question": "Which?", "header": "Pick", "multiSelect": false,
                "options": [{"label": "A"}, {"label": "B"}]}]}});
        let r = request_of(
            2,
            "claude:s".into(),
            "claude",
            "PermissionRequest",
            &payload,
            Duration::from_secs(15),
        );
        assert!(r.is_none(), "no approval card for the question tool");
        let r = request_of(
            2,
            "claude:s".into(),
            "claude",
            "PreToolUse",
            &payload,
            Duration::from_secs(15),
        );
        assert_eq!(
            r.map(|r| r.kind),
            Some(AttKind::Question),
            "the question itself is still offered"
        );
    }

    #[tokio::test]
    async fn approve_releases_hook_and_clears_device() {
        let (b, mut rx) = broker();
        let (rid, wait) = b.open(perm(false));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((att.rid, att.sid, att.kind), (rid, 2, AttKind::Approval));
        assert_eq!(att.title.as_deref(), Some("Run shell command"));
        assert_eq!(att.text.as_deref(), Some("npm test"));
        assert!(att.opts.is_none() && att.grant_mask.is_none());
        assert_eq!(att.acts, vec![Action::Approve, Action::Deny, Action::Focus]);

        let out = b.answer(rid, Decision::Approve).unwrap();
        assert_eq!(out.decision, "allow");
        assert_eq!(wait.await.unwrap(), out, "the hook gets the same outcome");
        let Update::Clear(c) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (rid, ClearWhy::AnsweredDev));
        assert!(b.pending().is_empty());
    }

    #[tokio::test]
    async fn deny_and_reply_map_to_the_agents_choices() {
        let (b, mut rx) = broker();
        let (rid, wait) = b.open(perm(true));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!(
            att.opts.as_deref(),
            Some(&["Yes".to_owned(), "Yes, don't ask again".into(), "No".into()][..])
        );
        assert_eq!(att.grant_mask, Some(0b011));
        assert!(att.acts.contains(&Action::Reply));
        let out = b.answer(rid, Decision::Reply(1)).unwrap();
        assert_eq!((out.decision, out.grant), ("allow", true));
        assert_eq!(wait.await.unwrap(), out);

        let (rid, wait) = b.open(perm(true));
        assert_eq!(b.answer(rid, Decision::Reply(2)).unwrap().decision, "deny");
        assert_eq!(wait.await.unwrap().decision, "deny");

        let (rid, wait) = b.open(perm(false));
        assert_eq!(b.answer(rid, Decision::Deny).unwrap().decision, "deny");
        assert_eq!(wait.await.unwrap().decision, "deny");

        let (rid, _wait) = b.open(perm(true));
        assert_eq!(
            b.answer(rid, Decision::Reply(7)).unwrap_err().0,
            ResultErr::Denied
        );
        assert_eq!(b.pending().len(), 1, "a bad index leaves the request open");
    }

    #[tokio::test]
    async fn question_reply_carries_the_label() {
        let (b, mut rx) = broker();
        let payload = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "Which framework?", "header": "Framework",
             "options": [{"label": "React"}, {"label": "Vue"}, {"label": "Keep vanilla"}]}]}});
        let req = request_of(
            1,
            "claude:s".into(),
            "claude",
            "PreToolUse",
            &payload,
            Duration::from_secs(15),
        )
        .unwrap();
        let (rid, wait) = b.open(req);
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((att.kind, att.grant_mask), (AttKind::Question, Some(0)));
        assert_eq!(att.acts, vec![Action::Focus, Action::Reply]);
        assert_eq!(att.title.as_deref(), Some("Framework"));
        let out = b.answer(rid, Decision::Reply(1)).unwrap();
        assert_eq!(
            (out.decision, out.answer.as_deref()),
            ("allow", Some("Vue"))
        );
        assert_eq!(wait.await.unwrap().answer.as_deref(), Some("Vue"));
        // approve is not an answer to a question
        let (rid, _w) = b.open(
            request_of(
                1,
                "claude:s".into(),
                "claude",
                "PreToolUse",
                &payload,
                Duration::from_secs(1),
            )
            .unwrap(),
        );
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Denied
        );
    }

    #[test]
    fn questions_the_wheel_cannot_answer_become_notices() {
        let ask = |v: Value| {
            request_of(
                1,
                "k".into(),
                "claude",
                "PreToolUse",
                &v,
                Duration::from_secs(15),
            )
        };
        // two questions, or a multi-select one: shown with Focus only, and the hook is not held
        let two = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "a", "options": [{"label": "x"}]}, {"question": "b", "options": [{"label": "y"}]}]}});
        let r = ask(two).expect("a notice");
        assert!(r.notice && r.focus_only && r.opts.is_empty());
        assert_eq!(
            (r.title.as_deref(), r.ttl),
            (Some("2 questions"), NOTICE_TTL)
        );
        let multi = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "Which parts?", "header": "Parts", "multiSelect": true, "options": [{"label": "x"}]}]}});
        let r = ask(multi).expect("a notice");
        assert!(r.notice && r.focus_only);
        assert_eq!(r.title.as_deref(), Some("Parts"));
        assert_eq!(r.text.as_deref(), Some("Which parts? (pick on the PC)"));
        // a single-select question is still answered on the watch
        let one = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "a", "options": [{"label": "x"}, {"label": "y"}]}]}});
        let r = ask(one).expect("a question");
        assert!(!r.notice && !r.focus_only && r.opts.len() == 2);
        // nothing for other tools' PreToolUse, or for agents without a decision path
        let other_tool = json!({"tool_name": "Bash", "tool_input": {}});
        assert!(ask(other_tool.clone()).is_none());
        assert!(request_of(
            1,
            "k".into(),
            "copilot",
            "PermissionRequest",
            &other_tool,
            Duration::from_secs(1)
        )
        .is_none());
    }

    #[tokio::test]
    async fn answer_with_the_hook_gone_is_stale_but_still_clears() {
        let (b, mut rx) = broker();
        let (rid, wait) = b.open(perm(false));
        let _ = next_update(&mut rx);
        drop(wait);
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err(),
            (ResultErr::Stale, "hook gone")
        );
        assert!(b.pending().is_empty());
        let Update::Clear(c) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (rid, ClearWhy::AnsweredDev));
    }

    #[tokio::test]
    async fn stale_rid_and_timeout_give_no_decision() {
        let (b, mut rx) = broker();
        assert_eq!(
            b.answer(99, Decision::Approve).unwrap_err().0,
            ResultErr::Stale
        );

        let (rid, wait) = b.open(perm(false));
        let _ = next_update(&mut rx);
        // the hook's wait runs out: cancel as expired, the hook sees no decision at all
        assert!(b.cancel(rid, ClearWhy::Expired));
        assert!(
            wait.await.is_err(),
            "a closed channel is 'no decision', never approve"
        );
        let Update::Clear(c) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!(c.why, ClearWhy::Expired);
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Stale
        );
        assert!(!b.cancel(rid, ClearWhy::Expired));
    }

    #[tokio::test]
    async fn first_answer_wins() {
        let (b, mut rx) = broker();
        // device first: the second device command is stale
        let (rid, wait) = b.open(perm(false));
        b.answer(rid, Decision::Deny).unwrap();
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Stale
        );
        assert_eq!(wait.await.unwrap().decision, "deny");
        // terminal first: a later event from the session cancels, then the device is stale
        let (rid, wait) = b.open(perm(false));
        let (other, _w2) = b.open(
            request_of(
                3,
                "claude:t".into(),
                "claude",
                "PermissionRequest",
                &json!({"tool_name": "Edit", "tool_input": {"file_path": "/x"}}),
                Duration::from_secs(5),
            )
            .unwrap(),
        );
        b.cancel_session("claude:s", Some("Bash"));
        assert!(wait.await.is_err());
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Stale
        );
        assert_eq!(
            b.pending().iter().map(|a| a.rid).collect::<Vec<_>>(),
            vec![other],
            "other sessions untouched"
        );
        while rx.try_recv().is_ok() {}
        b.cancel_session("claude:t", None);
        let Update::Clear(c) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (other, ClearWhy::AnsweredPc));
    }

    fn bash(cmd: &str, ttl: Duration) -> Request {
        let payload = json!({"tool_name": "Bash", "tool_input": {"command": cmd}});
        request_of(
            2,
            "claude:s".into(),
            "claude",
            "PermissionRequest",
            &payload,
            ttl,
        )
        .unwrap()
    }

    #[test]
    fn the_full_text_spells_out_what_the_watch_cannot_draw() {
        assert_eq!(detail_text("a\r\nb\tc\u{1b}[0m"), "a\nb^Ic^[[0m");
        assert_eq!(
            detail_text("caf\u{e9} \u{2014} ok\u{2026}"),
            "caf\u{e9} \u{2014} ok\u{2026}"
        );
        // invisible or look-alike characters are never shown as if they were not there
        assert_eq!(detail_text("rm\u{a0}-rf"), "rm\\u{a0}-rf");
        assert_eq!(detail_text("x\u{ad}y\u{200b}z"), "x\\u{ad}y\\u{200b}z");
        assert_eq!(detail_text("\u{1F600}\u{7f}\u{85}"), "\\u{1f600}^?\\u{85}");
        assert_eq!(
            detail_text("echo \"a    b\""),
            "echo \"a    b\"",
            "spaces are not collapsed"
        );
    }

    #[tokio::test]
    async fn the_window_rides_on_the_attention_and_counts_down() {
        let (b, mut rx) = broker();
        let (rid, _w) = b.open(bash("npm test", Duration::from_secs(60)));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((att.left, att.win), (Some(60_000), Some(60_000)));
        assert_eq!(att.len, None, "the preview is the whole text");
        tokio::time::sleep(Duration::from_millis(120)).await;
        // a device that connects now counts down from what is left, not from the start
        let again = b.pending().pop().unwrap();
        assert_eq!(again.rid, rid);
        let left = again.left.unwrap();
        assert!(left <= 59_880 && left > 59_000, "left {left}");
        assert_eq!(again.win, Some(60_000));
    }

    #[tokio::test]
    async fn a_notice_has_no_countdown() {
        let (b, mut rx) = broker();
        let payload = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "a", "options": [{"label": "x"}]}, {"question": "b", "options": [{"label": "y"}]}]}});
        let req = request_of(
            1,
            "claude:s".into(),
            "claude",
            "PreToolUse",
            &payload,
            Duration::from_secs(60),
        )
        .unwrap();
        b.open(req);
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((att.left, att.win), (None, None));
    }

    #[tokio::test]
    async fn a_long_or_reshaped_text_offers_its_full_text_by_rid() {
        let (b, mut rx) = broker();
        let long = format!("echo start && {} && echo end", "x".repeat(400));
        let (rid, _w) = b.open(bash(&long, Duration::from_secs(60)));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert!(att.text.as_ref().unwrap().ends_with('…'));
        assert_eq!(att.len, Some(long.len() as u64));
        assert_eq!(b.detail(rid).unwrap(), (2, long.clone()));
        // a short text whose preview had to be reshaped (two lines become one) is not whole either
        let (rid2, _w2) = b.open(bash("cd /tmp\nrm -rf build", Duration::from_secs(60)));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!(att.text.as_deref(), Some("cd /tmp rm -rf build"));
        assert_eq!(att.len, Some(20));
        assert_eq!(b.detail(rid2).unwrap().1, "cd /tmp\nrm -rf build");
        // detail is bound to the request: another rid, or a settled one, is stale
        assert_eq!(b.detail(rid + 1000).unwrap_err().0, ResultErr::Stale);
        b.cancel(rid, ClearWhy::Expired);
        assert_eq!(b.detail(rid).unwrap_err().0, ResultErr::Stale);
    }

    #[tokio::test]
    async fn past_the_cap_the_full_text_is_refused() {
        let (b, mut rx) = broker();
        let huge = "y".repeat(DETAIL_CAP + 1);
        let (rid, _w) = b.open(bash(&huge, Duration::from_secs(60)));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!(
            att.len,
            Some(DETAIL_CAP as u64 + 1),
            "the device learns it is too long"
        );
        assert_eq!(b.detail(rid).unwrap_err().0, ResultErr::Denied);
        let fits = "z".repeat(DETAIL_CAP);
        let (rid, _w) = b.open(bash(&fits, Duration::from_secs(60)));
        assert_eq!(b.detail(rid).unwrap().1.len(), DETAIL_CAP);
    }

    #[tokio::test]
    async fn handing_back_releases_the_hook_with_no_decision() {
        let (b, mut rx) = broker();
        let (rid, wait) = b.open(perm(false));
        let _ = next_update(&mut rx);
        assert!(
            !b.hand_back(rid, Some(99)),
            "another session's Open on PC hands nothing back"
        );
        assert!(
            !b.hand_back(rid + 1, Some(2)),
            "nor does another request id"
        );
        assert!(b.hand_back(rid, Some(2)));
        assert!(wait.await.is_err(), "no decision: the terminal asks");
        let Update::Clear(c) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (rid, ClearWhy::HandedBack));
        // nothing can answer it afterwards, not even the hold that was already running
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Stale
        );
        assert!(!b.hand_back(rid, Some(2)));
    }

    #[tokio::test]
    async fn linked_devices_are_counted_while_their_guard_lives() {
        let (b, _rx) = broker();
        assert_eq!(b.devices(), 0);
        let one = b.link();
        let two = b.link();
        assert_eq!(b.devices(), 2);
        drop(one);
        assert_eq!(b.devices(), 1);
        drop(two);
        assert_eq!(b.devices(), 0);
    }

    #[test]
    fn text_is_tidied_and_clipped() {
        assert_eq!(
            tidy("npm\ttest\n\n-- --watch\u{7}=false  "),
            "npm test -- --watch=false"
        );
        let long = "x".repeat(200);
        let (b, _rx) = broker();
        let mut r = perm(false);
        r.text = Some(long);
        let (_, _w) = b.open(r);
        let att = b.pending().pop().unwrap();
        assert_eq!(att.text.unwrap().chars().count(), 160);
    }

    #[tokio::test]
    async fn a_dictated_answer_goes_only_to_a_card_that_takes_one() {
        let (b, _rx) = broker();
        let card = |dictate: bool| Request {
            sid: 1,
            key: "copilot:s".into(),
            tool: Some("ask_user".into()),
            kind: AttKind::Question,
            title: None,
            text: None,
            opts: Vec::new(),
            ttl: Duration::from_secs(5),
            focus_only: !dictate,
            dictate,
            notice: false,
        };
        let (plain, _w1) = b.open(card(false));
        let (text, w2) = b.open(card(true));
        assert!(b
            .pending()
            .iter()
            .any(|a| a.rid == text && a.acts == [Action::Focus, Action::Dictate]));
        assert!(!b.takes_dictation(plain) && b.takes_dictation(text));
        assert_eq!(
            b.answer_text(plain, "hi".into()).unwrap_err().0,
            ResultErr::Denied
        );
        // a dictation card has no options to pick
        assert_eq!(
            b.answer(text, Decision::Reply(0)).unwrap_err().0,
            ResultErr::Denied
        );
        b.answer_text(text, "hi".into()).unwrap();
        assert_eq!(w2.await.unwrap().answer.as_deref(), Some("hi"));
        assert_eq!(
            b.answer_text(text, "again".into()).unwrap_err().0,
            ResultErr::Stale
        );
    }

    #[tokio::test]
    async fn a_tool_ending_leaves_another_tools_card() {
        let (b, _rx) = broker();
        let card = |tool: Option<&str>| Request {
            sid: 1,
            key: "copilot:s".into(),
            tool: tool.map(str::to_owned),
            kind: AttKind::Question,
            title: None,
            text: None,
            opts: Vec::new(),
            ttl: Duration::from_secs(5),
            focus_only: true,
            dictate: false,
            notice: false,
        };
        let (ask, _w1) = b.open(card(Some("ask_user")));
        let (screen, w2) = b.open(card(None));
        // a parallel `bash` call finishing: the form is still up, a screen-read prompt is not
        b.cancel_session("copilot:s", Some("bash"));
        assert!(w2.await.is_err());
        let left: Vec<u64> = b.pending().iter().map(|a| a.rid).collect();
        assert_eq!(left, vec![ask]);
        assert_ne!(ask, screen);
        b.cancel_session("copilot:s", Some("ask_user"));
        assert!(b.pending().is_empty());
    }
}
