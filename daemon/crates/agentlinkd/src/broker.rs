//! Approval broker: one pending hook response per request id (DESIGN §6a, UI-V2 §7).
//!
//! A blocking agent hook (Claude Code `PermissionRequest`, `AskUserQuestion`'s questions included)
//! opens a request here and waits on the returned channel. Devices see it as an `attention` with
//! the agent's own choices (`opts`, `grant_mask`, `qs`); their `approve` / `deny` / `reply` commands
//! land in [`Broker::answer`], `answer` in [`Broker::answer_questions`]. **First answer wins**:
//! the request is removed the moment it is decided, cancelled or expired, so a late command
//! gets `stale`, and a hook whose channel closes without a decision reports *no* decision,
//! never an approval.

use crate::registry::{clip, now_unix, Registry, Update};
use crate::standing::Standing;
use agentlink_proto::{
    Action, AskOpt, AskQ, AttKind, Attention, Clear, ClearWhy, Envelope, Message, ResultErr,
    RuleScope,
};
use serde::Serialize;
use serde_json::{Map, Value};
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
/// Bytes an attention with questions may take encoded (LAST-MESSAGE.md §3.2): descriptions are
/// shortened, then dropped, to stay well inside the watch's 4096-byte message limit.
const QS_BUDGET: usize = 3000;
/// Byte limits of a question's parts as the watch shows them (`attention.qs`).
const QS_HEADER: usize = 16;
const QS_QUESTION: usize = 160;
const QS_LABEL: usize = 48;
const QS_DESC: usize = 120;
const QS_DESC_SHORT: usize = 60;

/// What the device answered.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Decision {
    Approve,
    Deny,
    /// Index into the request's `opts`.
    Reply(usize),
    /// Approve, and save the request's standing rule (STANDING-RULES.md): the hash of the rule the
    /// device showed, which must be the hash of the request's own.
    Grant(u32),
}

/// What one of the request's options means to the agent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Meaning {
    Allow,
    Deny,
    /// A question's answer: the option's full label.
    Answer(String),
}

/// A standing rule the device granted, for the audit log (never sent to hooks).
#[derive(Debug, Clone, PartialEq)]
pub struct Saved {
    pub rule: String,
    pub scope: RuleScope,
    pub wide: bool,
    /// Source session key (`agent:session_id`).
    pub key: String,
}

/// What the hook is told; it turns this into the agent's own hook-output JSON.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Outcome {
    /// `allow` | `deny` | `none` (no decision: let the agent prompt as usual).
    pub decision: &'static str,
    /// With `allow`: permission update entries to apply, each one of the request's own
    /// `permission_suggestions` verbatim (STANDING-RULES.md). The hook passes them back as
    /// `updatedPermissions` only if each is one it received.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub permissions: Option<Vec<Value>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub answer: Option<String>,
    /// With `allow` on a question request (AskUserQuestion): `{question text: answer}` for every
    /// question, which the hook passes on as `updatedInput.answers` (LAST-MESSAGE.md §3.2).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub answers: Option<Map<String, Value>>,
    /// Index of the option the decision stands for (Approve: the first allow, Deny: the first
    /// deny), so a typed answer knows which key to press. Not sent to hooks.
    #[serde(skip)]
    pub choice: Option<usize>,
    /// The standing rule this answer saves, for the audit log. Not sent to hooks.
    #[serde(skip)]
    pub saved: Option<Saved>,
}

impl Outcome {
    pub const NONE: Outcome = Outcome {
        decision: "none",
        permissions: None,
        answer: None,
        answers: None,
        choice: None,
        saved: None,
    };

    fn of(decision: &'static str, answer: Option<String>, choice: Option<usize>) -> Self {
        Outcome {
            decision,
            permissions: None,
            answer,
            answers: None,
            choice,
            saved: None,
        }
    }

    /// `allow` with answers to a question request.
    fn answers(answers: Map<String, Value>, choice: Option<usize>) -> Self {
        Outcome {
            answers: Some(answers),
            ..Outcome::of("allow", None, choice)
        }
    }
}

/// One question of an `AskUserQuestion` request as the agent asked it. Nothing here is clipped: the
/// answers are keyed by the exact question text and carry the exact labels.
#[derive(Debug, Clone, PartialEq)]
pub struct Question {
    pub header: String,
    pub question: String,
    /// Label and description, in the agent's order.
    pub options: Vec<(String, Option<String>)>,
    /// Multi-select: any number of options, at least one.
    pub multi: bool,
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
    /// The standing rule the device may grant with an approve (STANDING-RULES.md).
    pub standing: Option<Standing>,
    /// An agent's questions, answered all at once with `cmd answer` (proto 0.18); empty = none.
    pub questions: Vec<Question>,
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
    /// The standing rule a grant saves; `att.rule` is its text.
    standing: Option<Standing>,
    /// The questions `att.qs` shows, unclipped.
    questions: Vec<Question>,
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
/// become `\u{…}`. Nothing is collapsed or dropped, except trailing line breaks and spaces (which show
/// nothing either way).
pub fn detail_text(s: &str) -> String {
    let s = s.replace("\r\n", "\n");
    let s = s.trim_end_matches([' ', '\n', '\r']);
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
pub(crate) fn first_rid() -> u64 {
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
    #[cfg(test)]
    pub fn open(&self, req: Request) -> (u64, oneshot::Receiver<Outcome>) {
        self.open_with(req, false)
    }

    /// [`Self::open`], with `hush`: tell the devices not to buzz for it (presence.rs). The card is
    /// shown all the same and can be answered as ever.
    pub fn open_with(&self, req: Request, hush: bool) -> (u64, oneshot::Receiver<Outcome>) {
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
            .filter(|(_, (_, m))| matches!(m, Meaning::Allow))
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
        // a standing rule goes only with a request the device can approve
        let standing = req
            .standing
            .filter(|_| req.kind == AttKind::Approval && !req.focus_only && !req.notice);
        let mut att = Attention {
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
            hush: hush.then_some(true),
            rule: standing.as_ref().map(|s| s.text.clone()),
            scope: standing.as_ref().map(|s| s.scope),
            wide: standing.as_ref().map(|s| s.wide),
            qs: None,
        };
        if !req.questions.is_empty() {
            fit_qs(&mut att, &req.questions);
        }
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
                standing,
                questions: req.questions,
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
                Outcome::of("allow", None, first(|m| matches!(m, Meaning::Allow)))
            }
            Decision::Grant(h) if p.att.kind == AttKind::Approval => match &p.standing {
                // the rule the device showed is the rule this request offers: hand back that entry only
                Some(s) if s.hash == h => Outcome {
                    permissions: Some(vec![s.entry.clone()]),
                    saved: Some(Saved {
                        rule: s.text.clone(),
                        scope: s.scope,
                        wide: s.wide,
                        key: p.key.clone(),
                    }),
                    ..Outcome::of("allow", None, first(|m| matches!(m, Meaning::Allow)))
                },
                Some(_) => return Err((ResultErr::Denied, "not the rule this request offers")),
                None => return Err((ResultErr::Denied, "no rule to save for this request")),
            },
            Decision::Deny if p.att.kind == AttKind::Approval => {
                Outcome::of("deny", None, first(|m| matches!(m, Meaning::Deny)))
            }
            Decision::Reply(i) => match p.meanings.get(i) {
                Some(Meaning::Allow) => Outcome::of("allow", None, Some(i)),
                Some(Meaning::Deny) => Outcome::of("deny", None, Some(i)),
                // AskUserQuestion's one question, from a device that knows no `qs`
                Some(Meaning::Answer(a)) => match p.questions.first() {
                    Some(q) => Outcome::answers(answer_of(q, a.clone()), Some(i)),
                    None => Outcome::of("allow", Some(a.clone()), Some(i)),
                },
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
        let outcome = match p.questions.first() {
            Some(q) => Outcome::answers(answer_of(q, text), None),
            None => Outcome::of("allow", Some(text), None),
        };
        self.settle(g, rid, outcome)
    }

    /// A device's answers to request `rid`'s questions (`cmd answer`, LAST-MESSAGE.md §3.2): per
    /// question, the options picked as a bit mask or the free answer's text (a transcript). Refused,
    /// with the request left open, unless there is one entry per question and each is either a pick
    /// (exactly one option on a single-choice question, only bits of its options) or a non-empty
    /// text, never both. Several labels are joined with `", "` in option order.
    pub fn answer_questions(
        &self,
        rid: u64,
        picks: &[u64],
        texts: &[Option<String>],
    ) -> Result<Outcome, (ResultErr, &'static str)> {
        let g = self.inner.lock().unwrap();
        let Some(p) = g.pending.get(&rid) else {
            return Err((ResultErr::Stale, "no pending request for rid"));
        };
        if p.questions.is_empty() {
            return Err((ResultErr::Denied, "no questions to answer"));
        }
        if picks.len() != p.questions.len() || texts.len() != picks.len() {
            return Err((ResultErr::Denied, "one entry per question"));
        }
        let mut answers = Map::new();
        for ((q, &pick), text) in p.questions.iter().zip(picks).zip(texts) {
            let value = match text.as_deref().map(str::trim) {
                Some(_) if pick != 0 => return Err((ResultErr::Denied, "a pick and a recording")),
                Some("") => return Err((ResultErr::Denied, "an empty answer")),
                Some(t) => t.to_owned(),
                None if pick == 0 => return Err((ResultErr::Denied, "nothing picked")),
                None if pick >> q.options.len() != 0 => {
                    return Err((ResultErr::Denied, "no such option"))
                }
                None if !q.multi && pick.count_ones() != 1 => {
                    return Err((ResultErr::Denied, "one option on this question"))
                }
                None => q
                    .options
                    .iter()
                    .enumerate()
                    .filter(|(i, _)| pick & (1 << i) != 0)
                    .map(|(_, (label, _))| label.as_str())
                    .collect::<Vec<_>>()
                    .join(", "),
            };
            answers.insert(q.question.clone(), Value::String(value));
        }
        self.settle(g, rid, Outcome::answers(answers, None))
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
            Decision::Approve | Decision::Grant(_) => p.att.kind == AttKind::Approval,
            Decision::Deny => false,
            Decision::Reply(i) => matches!(p.meanings.get(*i), Some(Meaning::Allow)),
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

    /// The session of open request `rid`.
    pub fn sid_of(&self, rid: u64) -> Option<u64> {
        self.inner
            .lock()
            .unwrap()
            .pending
            .get(&rid)
            .map(|p| p.att.sid)
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

/// How long a notice card (a question only the terminal can answer) may stay up if the session
/// never moves on; normally the next hook event clears it.
pub const NOTICE_TTL: Duration = Duration::from_secs(600);

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
        // Claude Code asks AskUserQuestion's questions through PermissionRequest; the answers go back
        // in `updatedInput` (LAST-MESSAGE.md §3). Its PreToolUse holds nothing (an older hook that still
        // waits on it gets no decision at once), so a question is never held twice.
        "permissionrequest" if tool.as_deref() == Some("AskUserQuestion") => {
            question_request(sid, key, tool, input, ttl)
        }
        "permissionrequest" => {
            let tool_name = tool.as_deref().unwrap_or("Tool");
            let (title, text) = describe_tool(tool_name, input);
            // Approve / Deny, and "Always allow" when Claude Code suggests a rule the watch can show in
            // full (standing.rs). Before 0.13 this was a "Yes, don't ask again" option that applied every
            // suggestion unseen, a permission mode change included.
            Some(Request {
                sid,
                key,
                tool,
                kind: AttKind::Approval,
                title: Some(title),
                text,
                opts: Vec::new(),
                ttl,
                focus_only: false,
                dictate: false,
                notice: false,
                standing: crate::standing::pick(payload),
                questions: Vec::new(),
            })
        }
        _ => None,
    }
}

/// AskUserQuestion's request: every question for a device that answers them (`qs`), and for an older
/// one the options of a lone single-choice question (`opts`, as the old PreToolUse path offered) or
/// Open on PC only. A malformed question (no options, more than four of anything, two alike) is a
/// notice: shown with Open on PC while the terminal asks at once.
fn question_request(
    sid: u64,
    key: String,
    tool: Option<String>,
    input: &Value,
    ttl: Duration,
) -> Option<Request> {
    let qs = input["questions"].as_array().filter(|a| !a.is_empty())?;
    let header = |q: &Value| {
        q["header"]
            .as_str()
            .map(str::trim)
            .filter(|h| !h.is_empty())
            .unwrap_or("Question")
            .to_owned()
    };
    let title = if qs.len() > 1 {
        format!("{} questions", qs.len())
    } else {
        header(&qs[0])
    };
    let parse = |q: &Value| {
        let question = q["question"].as_str().filter(|t| !t.trim().is_empty())?;
        let options = q["options"]
            .as_array()
            .filter(|o| (1..=4).contains(&o.len()))?
            .iter()
            .map(|o| {
                let label = o["label"].as_str().filter(|l| !l.trim().is_empty())?;
                Some((
                    label.to_owned(),
                    o["description"].as_str().map(str::to_owned),
                ))
            })
            .collect::<Option<Vec<_>>>()?;
        Some(Question {
            header: header(q),
            question: question.to_owned(),
            options,
            multi: q["multiSelect"].as_bool() == Some(true),
        })
    };
    let questions = (qs.len() <= 4)
        .then(|| qs.iter().map(parse).collect::<Option<Vec<_>>>())
        .flatten()
        // answers are keyed by the question's text: two alike could not both be answered
        .filter(|v| {
            v.iter()
                .enumerate()
                .all(|(i, q)| v[..i].iter().all(|o| o.question != q.question))
        });
    let Some(questions) = questions else {
        let first = &qs[0];
        let multi = first["multiSelect"].as_bool() == Some(true);
        return Some(Request {
            sid,
            key,
            tool,
            kind: AttKind::Question,
            title: Some(title),
            text: first["question"].as_str().map(|t| {
                let how = if multi { "pick" } else { "answer" };
                format!("{t} ({how} on the PC)")
            }),
            opts: Vec::new(),
            ttl: NOTICE_TTL,
            focus_only: true,
            dictate: false,
            notice: true,
            standing: None,
            questions: Vec::new(),
        });
    };
    let single = match questions.as_slice() {
        [q] if !q.multi => Some(q),
        _ => None,
    };
    let opts = single
        .map(|q| {
            q.options
                .iter()
                .map(|(l, _)| (l.clone(), Meaning::Answer(l.clone())))
                .collect()
        })
        .unwrap_or_default();
    Some(Request {
        sid,
        key,
        tool,
        kind: AttKind::Question,
        title: Some(title),
        text: Some(questions[0].question.clone()),
        opts,
        ttl,
        focus_only: single.is_none(),
        dictate: single.is_some(),
        notice: false,
        standing: None,
        questions,
    })
}

/// `{question: answer}` for a request's one question.
fn answer_of(q: &Question, answer: String) -> Map<String, Value> {
    Map::from_iter([(q.question.clone(), Value::String(answer))])
}

/// `qs` as the watch shows it: each part tidied and clipped to its byte limit, descriptions at most
/// `desc` bytes (0 = none).
fn ask_qs(questions: &[Question], desc: usize) -> Vec<AskQ> {
    let fit = |s: &str, max: usize| crate::voice::fit(&tidy(s), max);
    questions
        .iter()
        .map(|q| AskQ {
            h: fit(&q.header, QS_HEADER),
            q: fit(&q.question, QS_QUESTION),
            o: q.options
                .iter()
                .map(|(l, d)| AskOpt {
                    l: fit(l, QS_LABEL),
                    d: d.as_deref()
                        .filter(|_| desc > 0)
                        .map(|d| fit(d, desc))
                        .filter(|d| !d.is_empty()),
                })
                .collect(),
            m: q.multi.then_some(true),
        })
        .collect()
}

/// Sets `att.qs`, shortening and then dropping the descriptions until the encoded attention fits
/// [`QS_BUDGET`].
fn fit_qs(att: &mut Attention, questions: &[Question]) {
    for desc in [QS_DESC, QS_DESC_SHORT, 0] {
        att.qs = Some(ask_qs(questions, desc));
        let len = Envelope::new(u64::from(u32::MAX), Message::Attention(att.clone()))
            .to_bytes()
            .map_or(usize::MAX, |b| b.len());
        if len <= QS_BUDGET {
            return;
        }
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

    static SUGGESTION: std::sync::LazyLock<Value> = std::sync::LazyLock::new(|| {
        json!({"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "npm test:*"}],
               "behavior": "allow", "destination": "localSettings"})
    });

    fn perm(suggest: bool) -> Request {
        let payload = json!({"tool_name": "Bash", "tool_input": {"command": "npm test"},
                             "permission_suggestions": if suggest { json!([SUGGESTION.clone()]) } else { json!([]) }});
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

    #[test]
    fn a_hushed_request_is_still_a_whole_card_and_stays_hushed_when_re_sent() {
        let (b, mut rx) = broker();
        let (rid, _w) = b.open_with(perm(true), true);
        let Update::Attention(a) = next_update(&mut rx) else {
            panic!("an attention")
        };
        assert_eq!(a.hush, Some(true));
        // nothing else about the card changes: same actions, options and window as an unhushed one
        let (_, _w2) = b.open(perm(true));
        let Update::Attention(plain) = next_update(&mut rx) else {
            panic!("an attention")
        };
        assert_eq!(plain.hush, None, "never sent as false");
        assert_eq!(
            (&a.acts, &a.opts, a.grant_mask),
            (&plain.acts, &plain.opts, plain.grant_mask)
        );
        assert_eq!(a.win, plain.win);
        // a device that connects later still gets the hush (and the answer is still possible)
        let again = b.pending().into_iter().find(|p| p.rid == rid).unwrap();
        assert_eq!(again.hush, Some(true));
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

    fn ask(questions: Value) -> Option<Request> {
        request_of(
            1,
            "claude:s".into(),
            "claude",
            "PermissionRequest",
            &json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": questions}}),
            Duration::from_secs(15),
        )
    }

    fn two_questions() -> Value {
        json!([
            {"question": "Which framework?", "header": "Framework", "multiSelect": false,
             "options": [{"label": "React", "description": "Component model"}, {"label": "Vue"}]},
            {"question": "Which parts?", "header": "Parts", "multiSelect": true,
             "options": [{"label": "Router"}, {"label": "Store"}, {"label": "Tests"}]}])
    }

    #[test]
    fn showing_a_question_is_not_an_approval() {
        let r = ask(
            json!([{"question": "Which?", "header": "Pick", "multiSelect": false,
            "options": [{"label": "A"}, {"label": "B"}]}]),
        )
        .unwrap();
        assert_eq!(r.kind, AttKind::Question, "never an approval card");
        assert!(r.standing.is_none());
        // its PreToolUse holds nothing: the PermissionRequest is the one request
        let payload = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "Which?", "options": [{"label": "A"}]}]}});
        assert!(request_of(
            2,
            "claude:s".into(),
            "claude",
            "PreToolUse",
            &payload,
            Duration::from_secs(15)
        )
        .is_none());
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
        let mut r = perm(false);
        r.opts = vec![("Yes".into(), Meaning::Allow), ("No".into(), Meaning::Deny)];
        let (rid, wait) = b.open(r.clone());
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!(
            att.opts.as_deref(),
            Some(&["Yes".to_owned(), "No".into()][..])
        );
        assert_eq!(att.grant_mask, Some(0b01));
        assert!(att.acts.contains(&Action::Reply));
        let out = b.answer(rid, Decision::Reply(0)).unwrap();
        assert_eq!((out.decision, &out.permissions), ("allow", &None));
        assert_eq!(wait.await.unwrap(), out);

        let (rid, wait) = b.open(r.clone());
        assert_eq!(b.answer(rid, Decision::Reply(1)).unwrap().decision, "deny");
        assert_eq!(wait.await.unwrap().decision, "deny");

        let (rid, wait) = b.open(perm(false));
        assert_eq!(b.answer(rid, Decision::Deny).unwrap().decision, "deny");
        assert_eq!(wait.await.unwrap().decision, "deny");

        let (rid, _wait) = b.open(r);
        assert_eq!(
            b.answer(rid, Decision::Reply(7)).unwrap_err().0,
            ResultErr::Denied
        );
        assert_eq!(b.pending().len(), 1, "a bad index leaves the request open");
    }

    #[tokio::test]
    async fn a_standing_rule_is_offered_and_granted_verbatim() {
        let (b, mut rx) = broker();
        let (rid, wait) = b.open(perm(true));
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        // no options: Approve / Deny, and the rule with its scope and breadth
        assert!(att.opts.is_none() && att.grant_mask.is_none());
        assert_eq!(att.acts, vec![Action::Approve, Action::Deny, Action::Focus]);
        assert_eq!(att.rule.as_deref(), Some("Bash(npm test:*)"));
        assert_eq!(
            (att.scope, att.wide),
            (Some(RuleScope::Project), Some(true))
        );
        let h = crate::standing::hash("Bash(npm test:*)", RuleScope::Project, true);
        assert!(
            b.grants(rid, &Decision::Grant(h)),
            "a grant needs the full hold"
        );
        let out = b.answer(rid, Decision::Grant(h)).unwrap();
        assert_eq!(out.decision, "allow");
        assert_eq!(out.permissions, Some(vec![SUGGESTION.clone()]));
        let saved = out.saved.clone().unwrap();
        assert_eq!(
            (saved.rule.as_str(), saved.scope, saved.key.as_str()),
            ("Bash(npm test:*)", RuleScope::Project, "claude:s")
        );
        let to_hook = wait.await.unwrap();
        assert_eq!(to_hook, out);
        // what the hook reads: allow plus the one entry; the audit record is not in it
        let line = serde_json::to_value(&to_hook).unwrap();
        assert_eq!(
            line,
            json!({"decision": "allow", "permissions": [SUGGESTION.clone()]})
        );
        // answered: a second grant is stale
        assert_eq!(
            b.answer(rid, Decision::Grant(h)).unwrap_err().0,
            ResultErr::Stale
        );
    }

    #[tokio::test]
    async fn a_plain_approve_on_a_rule_request_saves_nothing() {
        let (b, _rx) = broker();
        let (rid, wait) = b.open(perm(true));
        let out = b.answer(rid, Decision::Approve).unwrap();
        assert_eq!((out.permissions, out.saved), (None, None));
        assert_eq!(
            serde_json::to_value(wait.await.unwrap()).unwrap(),
            json!({"decision": "allow"})
        );
    }

    #[tokio::test]
    async fn a_grant_must_name_the_rule_this_request_offers() {
        let (b, _rx) = broker();
        let (a, _wa) = b.open(perm(true));
        let mut other = perm(false);
        other.standing = crate::standing::pick(&json!({"permission_suggestions": [
            {"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "rm -rf build"}],
             "behavior": "allow", "destination": "session"}]}));
        let (o, wo) = b.open(other);
        let ha = crate::standing::hash("Bash(npm test:*)", RuleScope::Project, true);
        let ho = crate::standing::hash("Bash(rm -rf build)", RuleScope::Session, false);
        // the rule swapped between what was shown and what is held: denied, both still open
        assert_eq!(
            b.answer(o, Decision::Grant(ha)).unwrap_err().0,
            ResultErr::Denied
        );
        assert_eq!(
            b.answer(a, Decision::Grant(ho)).unwrap_err().0,
            ResultErr::Denied
        );
        // the same text with another scope or breadth is another rule
        let wrong_scope = crate::standing::hash("Bash(npm test:*)", RuleScope::Session, true);
        assert_eq!(
            b.answer(a, Decision::Grant(wrong_scope)).unwrap_err().0,
            ResultErr::Denied
        );
        assert_eq!(b.pending().len(), 2);
        // its own hash grants its own entry
        assert_eq!(
            b.answer(o, Decision::Grant(ho))
                .unwrap()
                .permissions
                .unwrap()[0]["rules"][0]["ruleContent"],
            "rm -rf build"
        );
        assert_eq!(wo.await.unwrap().decision, "allow");
        // a request with no rule cannot be granted one
        let (plain, _wp) = b.open(perm(false));
        assert_eq!(
            b.answer(plain, Decision::Grant(ha)).unwrap_err().0,
            ResultErr::Denied
        );
        // an unknown rid is stale
        assert_eq!(
            b.answer(u64::from(u32::MAX), Decision::Grant(ha))
                .unwrap_err()
                .0,
            ResultErr::Stale
        );
    }

    #[tokio::test]
    async fn a_grant_after_the_window_or_the_hand_back_is_stale() {
        let (b, _rx) = broker();
        let h = crate::standing::hash("Bash(npm test:*)", RuleScope::Project, true);
        let (rid, wait) = b.open(perm(true));
        assert!(b.cancel(rid, ClearWhy::Expired));
        assert_eq!(
            b.answer(rid, Decision::Grant(h)).unwrap_err().0,
            ResultErr::Stale
        );
        assert!(!b.grants(rid, &Decision::Grant(h)));
        assert!(wait.await.is_err(), "no decision reaches the hook");
        let (rid, wait) = b.open(perm(true));
        assert!(b.hand_back(rid, Some(2)));
        assert_eq!(
            b.answer(rid, Decision::Grant(h)).unwrap_err().0,
            ResultErr::Stale
        );
        assert!(wait.await.is_err());
        // the terminal answered (a later event from the session)
        let (rid, _wait) = b.open(perm(true));
        b.cancel_session("claude:s", None);
        assert_eq!(
            b.answer(rid, Decision::Grant(h)).unwrap_err().0,
            ResultErr::Stale
        );
    }

    #[test]
    fn only_a_request_the_device_can_approve_carries_a_rule() {
        let (b, _rx) = broker();
        let mut r = perm(true);
        r.focus_only = true;
        let (_, _w) = b.open(r);
        let mut q = perm(true);
        q.kind = AttKind::Question;
        let (_, _w2) = b.open(q);
        assert!(b
            .pending()
            .iter()
            .all(|a| a.rule.is_none() && a.scope.is_none() && a.wide.is_none()));
        // and the old "Yes, don't ask again" option is gone
        let r = perm(true);
        assert!(r.opts.is_empty() && r.standing.is_some());
    }

    #[tokio::test]
    async fn an_older_device_answers_a_lone_question_by_index_or_dictation() {
        let (b, mut rx) = broker();
        let one = json!([{"question": "Which framework?", "header": "Framework",
             "options": [{"label": "React"}, {"label": "Vue"}, {"label": "Keep vanilla"}]}]);
        let (rid, wait) = b.open(ask(one.clone()).unwrap());
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((att.kind, att.grant_mask), (AttKind::Question, Some(0)));
        assert_eq!(
            att.acts,
            vec![Action::Focus, Action::Reply, Action::Dictate]
        );
        assert_eq!(att.title.as_deref(), Some("Framework"));
        assert_eq!(att.text.as_deref(), Some("Which framework?"));
        assert_eq!(
            att.qs.as_ref().map(Vec::len),
            Some(1),
            "and the question for 0.18"
        );
        assert!(
            !b.grants(rid, &Decision::Reply(1)),
            "an answer is not a grant"
        );
        let out = b.answer(rid, Decision::Reply(1)).unwrap();
        assert_eq!((out.decision, out.answer.as_deref()), ("allow", None));
        let line = serde_json::to_value(wait.await.unwrap()).unwrap();
        assert_eq!(
            line,
            json!({"decision": "allow", "answers": {"Which framework?": "Vue"}})
        );
        // approve is not an answer to a question
        let (rid, wait) = b.open(ask(one).unwrap());
        assert_eq!(
            b.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Denied
        );
        // a dictated answer (dictate with the rid) is the question's free answer
        assert!(b.takes_dictation(rid));
        b.answer_text(rid, "Svelte, please".into()).unwrap();
        assert_eq!(
            wait.await.unwrap().answers.unwrap()["Which framework?"],
            "Svelte, please"
        );
    }

    #[tokio::test]
    async fn several_questions_are_answered_at_once_and_only_whole() {
        let (b, mut rx) = broker();
        let (rid, wait) = b.open(ask(two_questions()).unwrap());
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        // an older device: Open on PC only
        assert_eq!(att.acts, vec![Action::Focus]);
        assert_eq!((att.opts.as_ref(), att.grant_mask), (None, None));
        assert_eq!(att.title.as_deref(), Some("2 questions"));
        assert_eq!(att.text.as_deref(), Some("Which framework?"));
        assert_eq!(att.win, Some(15_000), "held with the answer window");
        let qs = att.qs.unwrap();
        assert_eq!(qs[0].h, "Framework");
        assert_eq!(qs[0].o[0].d.as_deref(), Some("Component model"));
        assert_eq!((qs[0].m, qs[1].m), (None, Some(true)));
        assert_eq!(
            qs[1].o.iter().map(|o| o.l.as_str()).collect::<Vec<_>>(),
            ["Router", "Store", "Tests"]
        );
        assert_eq!(
            b.answer(rid, Decision::Reply(0)).unwrap_err().0,
            ResultErr::Denied
        );
        let none = [None, None];
        for (picks, texts, why) in [
            (&[1u64][..], &none[..1], "one entry per question"),
            (
                &[1, 1, 1],
                &[None, None, None][..],
                "one entry per question",
            ),
            (&[0, 1], &none[..], "nothing picked"),
            (&[3, 1], &none[..], "one option on this question"),
            (&[1, 0b1000], &none[..], "no such option"),
            (&[4, 1], &none[..], "no such option"),
            (
                &[1, 1],
                &[None, Some("both".to_owned())][..],
                "a pick and a recording",
            ),
            (
                &[1, 0],
                &[None, Some("  ".to_owned())][..],
                "an empty answer",
            ),
        ] {
            assert_eq!(
                b.answer_questions(rid, picks, texts).unwrap_err(),
                (ResultErr::Denied, why)
            );
        }
        assert_eq!(b.pending().len(), 1, "a refused answer leaves it open");
        let out = b.answer_questions(rid, &[1, 0b101], &none).unwrap();
        let line = serde_json::to_value(&out).unwrap();
        assert_eq!(
            line,
            json!({"decision": "allow", "answers": {"Which framework?": "React", "Which parts?": "Router, Tests"}})
        );
        assert_eq!(wait.await.unwrap(), out);
        let Update::Clear(c) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (rid, ClearWhy::AnsweredDev));
        assert_eq!(
            b.answer_questions(rid, &[1, 1], &none).unwrap_err().0,
            ResultErr::Stale
        );
        // a free answer, and a request without questions
        let (rid, wait) = b.open(ask(two_questions()).unwrap());
        b.answer_questions(rid, &[0, 2], &[Some(" Svelte ".into()), None])
            .unwrap();
        let a = wait.await.unwrap().answers.unwrap();
        assert_eq!(
            (&a["Which framework?"], &a["Which parts?"]),
            (&json!("Svelte"), &json!("Store"))
        );
        let (rid, _w) = b.open(perm(false));
        assert_eq!(
            b.answer_questions(rid, &[1], &[None]).unwrap_err().0,
            ResultErr::Denied
        );
    }

    #[test]
    fn long_questions_are_clipped_for_the_watch_and_kept_whole_for_the_agent() {
        let (b, _rx) = broker();
        let long = |n: usize| "é".repeat(n);
        let qs: Vec<Value> = (0..4)
            .map(|i| json!({"question": format!("{i}{}", long(200)), "header": long(30),
                "options": (0..4).map(|j| json!({"label": format!("{j}{}", long(60)), "description": long(200)}))
                    .collect::<Vec<_>>()}))
            .collect();
        let (rid, _w) = b.open(ask(json!(qs)).unwrap());
        let att = b.pending().pop().unwrap();
        let shown = att.qs.clone().unwrap();
        assert_eq!(shown.len(), 4);
        for q in &shown {
            assert!(q.h.len() <= QS_HEADER && q.h.ends_with('…'));
            assert!(q.q.len() <= QS_QUESTION && q.q.ends_with('…'));
            for o in &q.o {
                assert!(o.l.len() <= QS_LABEL && o.l.ends_with('…'));
            }
        }
        let bytes = Envelope::new(1, Message::Attention(att))
            .to_bytes()
            .unwrap();
        assert!(bytes.len() <= QS_BUDGET, "{} bytes", bytes.len());
        assert!(
            shown
                .iter()
                .flat_map(|q| &q.o)
                .all(|o| o.d.as_deref().is_none_or(|d| d.len() <= QS_DESC_SHORT)),
            "descriptions shortened or dropped to fit"
        );
        // the answer uses the whole label, keyed by the whole question
        let out = b
            .answer_questions(rid, &[2, 1, 1, 8], &[None, None, None, None])
            .unwrap();
        let a = out.answers.unwrap();
        assert_eq!(a[&format!("0{}", long(200))], format!("1{}", long(60)));
        assert_eq!(a[&format!("3{}", long(200))], format!("3{}", long(60)));
    }

    #[test]
    fn malformed_questions_become_notices() {
        for bad in [
            json!([{"question": "a", "options": []}]),
            json!([{"question": "a"}]),
            json!([{"question": "a", "options": [{"label": "x"}, {"description": "no label"}]}]),
            json!([{"question": "a", "options": [{"label": "1"}, {"label": "2"}, {"label": "3"},
                {"label": "4"}, {"label": "5"}]}]),
            json!([{"question": "", "options": [{"label": "x"}]}]),
            json!([{"question": "a", "options": [{"label": "x"}]}, {"question": "a", "options": [{"label": "y"}]}]),
            json!((0..5)
                .map(|i| json!({"question": format!("q{i}"), "options": [{"label": "x"}]}))
                .collect::<Vec<_>>()),
        ] {
            let r = ask(bad.clone()).expect("a notice");
            assert!(r.notice && r.focus_only && r.opts.is_empty(), "{bad}");
            assert!(r.questions.is_empty(), "{bad}: no qs");
            assert_eq!(r.ttl, NOTICE_TTL);
        }
        let multi = json!([{"question": "Which parts?", "header": "Parts", "multiSelect": true,
            "options": [{"label": "x"}, {}]}]);
        let r = ask(multi).unwrap();
        assert_eq!(r.title.as_deref(), Some("Parts"));
        assert_eq!(r.text.as_deref(), Some("Which parts? (pick on the PC)"));
        // no questions at all: nothing to show
        assert!(ask(json!([])).is_none());
        assert!(ask(json!(null)).is_none());
        // nothing for other tools' PreToolUse, or for agents without a decision path
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
        b.open(ask(json!([{"question": "a", "options": []}])).unwrap());
        let Update::Attention(att) = next_update(&mut rx) else {
            panic!()
        };
        assert_eq!((att.left, att.win, &att.qs), (None, None, &None));
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
            standing: None,
            questions: Vec::new(),
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
            standing: None,
            questions: Vec::new(),
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
