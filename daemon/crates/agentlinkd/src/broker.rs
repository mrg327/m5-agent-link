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
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::sync::oneshot;

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
}

impl Outcome {
    pub const NONE: Outcome = Outcome {
        decision: "none",
        grant: false,
        answer: None,
    };
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
    pub ttl: Duration,
}

struct Pending {
    att: Attention,
    key: String,
    tool: Option<String>,
    meanings: Vec<Meaning>,
    tx: oneshot::Sender<Outcome>,
}

#[derive(Default)]
struct Inner {
    next_rid: u64,
    pending: HashMap<u64, Pending>,
}

pub struct Broker {
    inner: Mutex<Inner>,
    registry: Arc<Registry>,
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

impl Broker {
    pub fn new(registry: Arc<Registry>) -> Self {
        Broker {
            inner: Mutex::new(Inner {
                next_rid: 1,
                ..Default::default()
            }),
            registry,
        }
    }

    /// Registers the request, tells every device, and returns the channel the hook waits on.
    /// The channel yields `Err` when the request ends without a device decision.
    pub fn open(&self, req: Request) -> (u64, oneshot::Receiver<Outcome>) {
        let (tx, rx) = oneshot::channel();
        let mut acts = match req.kind {
            AttKind::Approval => vec![Action::Approve, Action::Deny, Action::Focus],
            _ => vec![Action::Focus],
        };
        if !req.opts.is_empty() {
            acts.push(Action::Reply);
        }
        let grant_mask = req
            .opts
            .iter()
            .enumerate()
            .filter(|(_, (_, m))| matches!(m, Meaning::Allow | Meaning::AllowGrant))
            .fold(0u64, |mask, (i, _)| mask | (1 << i));
        let mut g = self.inner.lock().unwrap();
        let rid = g.next_rid;
        g.next_rid += 1;
        let att = Attention {
            rid,
            sid: req.sid,
            kind: req.kind,
            title: req.title.map(|t| clip(&tidy(&t), 40)),
            text: req.text.map(|t| clip(&tidy(&t), 160)),
            opts: (!req.opts.is_empty())
                .then(|| req.opts.iter().map(|(o, _)| clip(&tidy(o), 24)).collect()),
            acts,
            exp: Some(now_unix() + req.ttl.as_secs().max(1)),
            grant_mask: (!req.opts.is_empty()).then_some(grant_mask),
        };
        g.pending.insert(
            rid,
            Pending {
                att: att.clone(),
                key: req.key,
                tool: req.tool,
                meanings: req.opts.into_iter().map(|(_, m)| m).collect(),
                tx,
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
        let mut g = self.inner.lock().unwrap();
        let Some(p) = g.pending.get(&rid) else {
            return Err((ResultErr::Stale, "no pending request for rid"));
        };
        let outcome = match decision {
            Decision::Approve if p.att.kind == AttKind::Approval => Outcome {
                decision: "allow",
                grant: false,
                answer: None,
            },
            Decision::Deny if p.att.kind == AttKind::Approval => Outcome {
                decision: "deny",
                grant: false,
                answer: None,
            },
            Decision::Reply(i) => match p.meanings.get(i) {
                Some(Meaning::Allow) => Outcome {
                    decision: "allow",
                    grant: false,
                    answer: None,
                },
                Some(Meaning::AllowGrant) => Outcome {
                    decision: "allow",
                    grant: true,
                    answer: None,
                },
                Some(Meaning::Deny) => Outcome {
                    decision: "deny",
                    grant: false,
                    answer: None,
                },
                Some(Meaning::Answer(a)) => Outcome {
                    decision: "allow",
                    grant: false,
                    answer: Some(a.clone()),
                },
                None => return Err((ResultErr::Denied, "no such option")),
            },
            _ => return Err((ResultErr::Denied, "not a permission request")),
        };
        let p = g.pending.remove(&rid).unwrap();
        drop(g);
        let _ = p.tx.send(outcome.clone()); // the hook may already be gone; the clear still goes out
        self.registry.publish(Update::Clear(Clear {
            rid,
            why: ClearWhy::AnsweredDev,
        }));
        Ok(outcome)
    }

    /// Ends a request without a device decision (timeout, terminal answer, hook gone).
    pub fn cancel(&self, rid: u64, why: ClearWhy) -> bool {
        let removed = self.inner.lock().unwrap().pending.remove(&rid);
        if removed.is_some() {
            self.registry.publish(Update::Clear(Clear { rid, why }));
        }
        removed.is_some()
    }

    /// The terminal moved on (a later event from the same session, optionally for the same
    /// tool): whatever was pending there was answered on the PC.
    pub fn cancel_session(&self, key: &str, tool: Option<&str>) {
        let rids: Vec<u64> = self
            .inner
            .lock()
            .unwrap()
            .pending
            .iter()
            .filter(|(_, p)| p.key == key && tool.is_none_or(|t| p.tool.as_deref() == Some(t)))
            .map(|(rid, _)| *rid)
            .collect();
        for rid in rids {
            self.cancel(rid, ClearWhy::AnsweredPc);
        }
    }

    /// Open requests, for a device that connects while they are pending.
    pub fn pending(&self) -> Vec<Attention> {
        let g = self.inner.lock().unwrap();
        let mut v: Vec<Attention> = g.pending.values().map(|p| p.att.clone()).collect();
        v.sort_by_key(|a| a.rid);
        v
    }
}

/// Builds the request a blocking hook line describes, or `None` when the watch cannot answer
/// it (then the hook gets `none` at once and the agent prompts as usual).
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
            })
        }
        "pretooluse" if tool.as_deref() == Some("AskUserQuestion") => {
            let qs = input["questions"].as_array()?;
            // One question with single-select options is what a wheel can answer.
            let [q] = qs.as_slice() else {
                return None;
            };
            if q["multiSelect"].as_bool() == Some(true) {
                return None;
            }
            let opts: Vec<(String, Meaning)> = q["options"]
                .as_array()?
                .iter()
                .filter_map(|o| o["label"].as_str())
                .take(4)
                .map(|l| (l.to_owned(), Meaning::Answer(l.to_owned())))
                .collect();
            if opts.is_empty() {
                return None;
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
    fn requests_the_watch_cannot_answer_are_none() {
        let two = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "a", "options": [{"label": "x"}]}, {"question": "b", "options": [{"label": "y"}]}]}});
        assert!(request_of(
            1,
            "k".into(),
            "claude",
            "PreToolUse",
            &two,
            Duration::from_secs(1)
        )
        .is_none());
        let multi = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "a", "multiSelect": true, "options": [{"label": "x"}]}]}});
        assert!(request_of(
            1,
            "k".into(),
            "claude",
            "PreToolUse",
            &multi,
            Duration::from_secs(1)
        )
        .is_none());
        let other_tool = json!({"tool_name": "Bash", "tool_input": {}});
        assert!(request_of(
            1,
            "k".into(),
            "claude",
            "PreToolUse",
            &other_tool,
            Duration::from_secs(1)
        )
        .is_none());
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
}
