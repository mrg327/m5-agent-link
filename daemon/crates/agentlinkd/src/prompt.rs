//! Answering an agent's own terminal prompt from the device by typing into its window
//! (DESIGN §6b). For agents whose hooks cannot return an answer (Copilot CLI): its permission
//! prompt and its `ask_user` form become `attention` cards, and a device answer is typed as
//! the keys a person would press, after re-reading the screen to check the same prompt is
//! still showing. Anything the screen check cannot confirm is left to the terminal.
//!
//! Copilot's prompts, as rendered by v1.0.10x:
//! - permission: a box ending in numbered options (`❯ 1. Yes`, `2. Yes, and don't ask again
//!   …`, `3. No, and tell Copilot …`) and `↑/↓ to navigate · enter to select · esc to cancel`;
//!   the digit picks an option at once.
//! - `ask_user`: `Field:` then one option per line (`❯` marks the cursor, `Other (type your
//!   answer)` last) and `↑/↓ select · enter accept · ctrl+d decline · esc cancel`; arrows
//!   move, Enter (sent on its own) accepts. A form with several fields shows them as tabs
//!   (a tab bar above the field, `tab next`/`shift+tab prev` in the footer), one at a time
//!   in schema order; Enter accepts the field and moves to the next, and submits on the last.

use crate::broker::{Broker, Meaning, Outcome, Request};
use crate::focus::{FocusError, SystemTerminal, Terminal};
use crate::registry::{clip, HostCtx, Registry, Route};
use crate::trace::Trace;
use agentlink_proto::{AttKind, ClearWhy};
use serde_json::{json, Value};
use std::sync::Arc;
use std::time::Duration;

/// A terminal prompt waits as long as it takes; the card goes when the session moves on.
const CARD_TTL: Duration = Duration::from_secs(600);
const KEY_GAP: Duration = Duration::from_millis(60);
const DOWN: &str = "\\x1b[B";
const UP: &str = "\\x1b[A";
const ENTER: &str = "\\r";
const OTHER: &str = "Other (type your answer)";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Permission,
    Choice,
}

/// The prompt at the bottom of a screen dump.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Screen {
    pub kind: Kind,
    /// The prompt's text above its options; identifies *this* prompt.
    pub body: Vec<String>,
    pub opts: Vec<String>,
    pub cursor: Option<usize>,
}

/// What the screen must show for a device answer to be typed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Expect {
    /// `map[i]` is the screen option (0-based) behind card option `i`.
    Permission {
        body: Vec<String>,
        n: usize,
        map: Vec<usize>,
    },
    /// The card's options are the first screen options, in order. `field` is the title the
    /// screen must show above them (`Title:`), when the form gave one.
    Choice {
        labels: Vec<String>,
        field: Option<String>,
    },
}

fn is_rule(s: &str) -> bool {
    s.chars().all(|c| "─━╭╮╰╯│┃╻╹▄▀ ".contains(c))
}

/// A screen line without the box borders around it.
fn clean(line: &str) -> &str {
    let mut s = line.trim();
    loop {
        let t = s
            .trim_start_matches(['│', '┃'])
            .trim_end_matches(['│', '┃'])
            .trim();
        if t.len() == s.len() {
            return s;
        }
        s = t;
    }
}

fn cursor_of(s: &str) -> (bool, &str) {
    match s.strip_prefix('❯') {
        Some(rest) => (true, rest.trim_start()),
        None => (false, s),
    }
}

/// `1. Yes` → (1, "Yes"), with or without the cursor mark.
fn numbered(s: &str) -> Option<(usize, &str)> {
    let (_, s) = cursor_of(s);
    let (n, rest) = s.split_once(". ")?;
    if n.is_empty() || n.len() > 1 || !n.chars().all(|c| c.is_ascii_digit()) {
        return None;
    }
    Some((n.parse().ok()?, rest.trim()))
}

pub fn parse(dump: &str) -> Option<Screen> {
    let raw: Vec<&str> = dump.lines().collect();
    let lines: Vec<&str> = raw.iter().map(|l| clean(l)).collect();
    let f = lines.iter().rposition(|l| {
        l.starts_with("↑/↓") && (l.contains("enter to select") || l.contains("enter accept"))
    })?;
    // Anything but blank lines and box edges below the footer means the prompt is gone.
    if lines[f + 1..].iter().any(|l| !l.is_empty() && !is_rule(l)) {
        return None;
    }
    if lines[f].contains("enter to select") {
        parse_permission(&raw, &lines, f)
    } else {
        parse_choice(&raw, &lines, f)
    }
}

fn parse_permission(raw: &[&str], lines: &[&str], f: usize) -> Option<Screen> {
    let mut opts: Vec<(usize, String)> = Vec::new();
    let mut cursor = None;
    let mut i = f;
    while i > 0 && f - i < 40 {
        i -= 1;
        let l = lines[i];
        if let Some((n, label)) = numbered(l) {
            if cursor_of(l).0 {
                cursor = Some(n - 1);
            }
            if opts.last().is_some_and(|(m, _)| *m != n + 1) {
                return None;
            }
            opts.push((n, label.to_owned()));
            if n == 1 {
                break;
            }
        } else if !opts.is_empty() && !l.is_empty() {
            // a wrapped option label: its continuation sits below its number
            continue;
        }
    }
    if opts.last().map(|o| o.0) != Some(1) || opts.len() < 2 {
        return None;
    }
    let first_opt = i;
    let top = (0..first_opt)
        .rev()
        .take(60)
        .find(|&j| raw[j].trim_start().starts_with('╭'))?;
    let body = lines[top + 1..first_opt]
        .iter()
        .filter(|l| !l.is_empty() && !is_rule(l))
        .map(|l| l.to_string())
        .collect();
    opts.reverse();
    Some(Screen {
        kind: Kind::Permission,
        body,
        opts: opts.into_iter().map(|o| o.1).collect(),
        cursor,
    })
}

fn parse_choice(_raw: &[&str], lines: &[&str], f: usize) -> Option<Screen> {
    let mut opts = Vec::new();
    let mut cursor = None;
    let mut i = f;
    loop {
        if i == 0 || f - i > 30 {
            return None;
        }
        i -= 1;
        let l = lines[i];
        if l.is_empty() {
            continue;
        }
        if l.ends_with(':') && !cursor_of(l).0 {
            break;
        }
        let (here, label) = cursor_of(l);
        if here {
            cursor = Some(opts.len());
        }
        opts.push(label.to_owned());
    }
    if opts.is_empty() {
        return None;
    }
    let cursor = cursor.map(|c| opts.len() - 1 - c);
    opts.reverse();
    let mut body = Vec::new();
    let mut j = i + 1;
    while j > 0 && i + 1 - j < 12 {
        j -= 1;
        let l = lines[j];
        if !l.is_empty() && is_rule(l) {
            break;
        }
        if !l.is_empty() {
            body.push(l.to_owned());
        }
    }
    body.reverse();
    Some(Screen {
        kind: Kind::Choice,
        body,
        opts,
        cursor,
    })
}

fn short_label(i: usize, label: &str) -> String {
    let lower = label.to_ascii_lowercase();
    if lower.starts_with("no") {
        "No".into()
    } else if i == 0 && lower == "yes" {
        "Yes".into()
    } else if lower.starts_with("yes") && lower.contains("don't ask again") {
        "Yes, don't ask again".into()
    } else if lower.starts_with("yes") && lower.contains("session") {
        "Yes, for this session".into()
    } else {
        clip(label, 24)
    }
}

/// Card and screen expectation for a permission prompt read from the screen; `None` screen
/// (unreadable terminal) gives a card that only offers Focus.
pub fn permission_request(
    sid: u64,
    key: String,
    message: &str,
    screen: Option<&Screen>,
) -> (Request, Option<Expect>) {
    let (title, text) = match message.split_once(": ") {
        Some((t, rest)) if !t.is_empty() && t.len() <= 40 => (t.to_owned(), rest.to_owned()),
        _ => ("Permission needed".to_owned(), message.to_owned()),
    };
    let mut req = Request {
        sid,
        key,
        tool: None,
        kind: AttKind::Approval,
        title: Some(title),
        text: (!text.is_empty()).then_some(text),
        opts: Vec::new(),
        ttl: CARD_TTL,
        focus_only: true,
    };
    let Some(s) = screen.filter(|s| s.kind == Kind::Permission && s.opts.len() <= 9) else {
        return (req, None);
    };
    // Four fit on the watch: keep the first three and the last (the "No").
    let map: Vec<usize> = if s.opts.len() <= 4 {
        (0..s.opts.len()).collect()
    } else {
        vec![0, 1, 2, s.opts.len() - 1]
    };
    req.opts = map
        .iter()
        .map(|&i| {
            let label = &s.opts[i];
            let lower = label.to_ascii_lowercase();
            let meaning = if lower.starts_with("no") {
                Meaning::Deny
            } else if i == 0 {
                Meaning::Allow
            } else {
                // "Yes, and …" and anything unknown: allow-class, so the device makes you hold
                Meaning::AllowGrant
            };
            (short_label(i, label), meaning)
        })
        .collect();
    req.focus_only = false;
    let expect = Expect::Permission {
        body: s.body.clone(),
        n: s.opts.len(),
        map,
    };
    (req, Some(expect))
}

/// The choices a form field is drawn with, or none for free text.
fn field_labels(field: &Value) -> Vec<String> {
    if let Some(e) = field["enum"].as_array() {
        e.iter()
            .filter_map(|v| v.as_str().map(str::to_owned))
            .collect()
    } else if let Some(o) = field["oneOf"].as_array() {
        o.iter()
            .filter_map(|v| v["title"].as_str().or(v["const"].as_str()))
            .map(str::to_owned)
            .collect()
    } else if field["type"].as_str() == Some("boolean") {
        // drawn as `Yes` / `No` (cursor on the default), then `Other`
        vec!["Yes".to_owned(), "No".to_owned()]
    } else {
        Vec::new()
    }
}

/// Cards and screen expectations for Copilot's `ask_user` (`toolArgs`: `message` +
/// `requestedSchema`): one card per field, in schema order, when every field has fixed
/// choices; otherwise a single Focus-only card.
pub fn ask_requests(sid: u64, key: String, tool_args: &Value) -> Vec<(Request, Option<Expect>)> {
    let parsed;
    let args = match tool_args.as_str() {
        Some(s) => {
            parsed = serde_json::from_str::<Value>(s).unwrap_or(Value::Null);
            &parsed
        }
        None => tool_args,
    };
    let message = args["message"].as_str().unwrap_or("").to_owned();
    let base = Request {
        sid,
        key,
        tool: Some("ask_user".into()),
        kind: AttKind::Question,
        title: Some("Question".into()),
        text: (!message.is_empty()).then(|| message.clone()),
        opts: Vec::new(),
        ttl: CARD_TTL,
        focus_only: true,
    };
    let fields: Vec<(Option<String>, Vec<String>)> = args["requestedSchema"]["properties"]
        .as_object()
        .map(|p| {
            p.values()
                .map(|f| {
                    let title = f["title"].as_str().filter(|t| !t.is_empty());
                    (title.map(str::to_owned), field_labels(f))
                })
                .collect()
        })
        .unwrap_or_default();
    if fields.is_empty() || fields.iter().any(|(_, l)| l.is_empty()) {
        if fields.len() == 1 {
            let mut req = base;
            if let Some(t) = &fields[0].0 {
                req.title = Some(t.clone());
            }
            return vec![(req, None)];
        }
        return vec![(base, None)];
    }
    let n = fields.len();
    fields
        .into_iter()
        .enumerate()
        .map(|(i, (title, labels))| {
            let labels: Vec<String> = labels.into_iter().take(4).collect();
            let mut req = base.clone();
            if let Some(t) = &title {
                req.title = Some(t.clone());
            }
            if n > 1 {
                req.text = Some(format!("({}/{n}) {message}", i + 1).trim_end().to_owned());
            }
            req.opts = labels
                .iter()
                .map(|l| (l.clone(), Meaning::Answer(l.clone())))
                .collect();
            req.focus_only = false;
            (
                req,
                Some(Expect::Choice {
                    labels,
                    field: title,
                }),
            )
        })
        .collect()
}

fn same_label(screen: &str, want: &str) -> bool {
    screen == want || (screen.len() >= 8 && want.starts_with(screen.trim_end_matches('…')))
}

fn read(term: &dyn Terminal, ctx: &HostCtx) -> Result<Screen, String> {
    let dump = term.read_screen(ctx).map_err(err)?;
    parse(&dump).ok_or_else(|| "the prompt is no longer on screen".to_owned())
}

fn err(e: FocusError) -> String {
    match e {
        FocusError::NoWindow => "no window found".into(),
        FocusError::Unsupported => "this terminal cannot be typed into".into(),
        FocusError::Internal(m) => m,
    }
}

/// Types card option `choice` into the terminal, if the screen still shows the expected prompt.
pub fn type_answer(
    term: &dyn Terminal,
    ctx: &HostCtx,
    expect: &Expect,
    choice: usize,
    gap: Duration,
) -> Result<(), String> {
    let s = read(term, ctx)?;
    match expect {
        Expect::Permission { body, n, map } => {
            if s.kind != Kind::Permission || s.body != *body || s.opts.len() != *n {
                return Err("a different prompt is on screen".into());
            }
            let idx = *map.get(choice).ok_or("no such option")?;
            term.send_keys(ctx, &(idx + 1).to_string()).map_err(err)
        }
        Expect::Choice { labels, field } => {
            if choice >= labels.len() {
                return Err("no such option".into());
            }
            let on_screen: Vec<&String> = s.opts.iter().filter(|o| *o != OTHER).collect();
            let field_ok = field.as_ref().is_none_or(|f| {
                s.body
                    .last()
                    .and_then(|l| l.strip_suffix(':'))
                    .is_some_and(|l| same_label(l, f))
            });
            if s.kind != Kind::Choice
                || !field_ok
                || on_screen.len() < labels.len()
                || !labels.iter().zip(&on_screen).all(|(w, o)| same_label(o, w))
            {
                return Err("a different prompt is on screen".into());
            }
            let cursor = s.cursor.ok_or("no cursor in the list")?;
            let moves = choice as i64 - cursor as i64;
            let key = if moves < 0 { UP } else { DOWN };
            for _ in 0..moves.unsigned_abs() {
                term.send_keys(ctx, key).map_err(err)?;
                std::thread::sleep(gap);
            }
            let s = read(term, ctx)?;
            if s.kind != Kind::Choice || s.cursor != Some(choice) {
                return Err("the cursor did not land on the answer".into());
            }
            term.send_keys(ctx, ENTER).map_err(err)
        }
    }
}

/// Everything the background tasks need.
#[derive(Clone)]
pub struct Env {
    pub registry: Arc<Registry>,
    pub broker: Arc<Broker>,
    pub trace: Trace,
    pub term: Arc<dyn Terminal>,
}

impl Env {
    pub fn system(registry: Arc<Registry>, broker: Arc<Broker>, trace: Trace) -> Self {
        Env {
            registry,
            broker,
            trace,
            term: Arc::new(SystemTerminal),
        }
    }
}

fn ctx_of(registry: &Registry, sid: u64) -> Result<HostCtx, String> {
    match registry.route(sid) {
        Route::Ready(_, ctx) => Ok(ctx),
        Route::Unsupported => Err("remote session without its agentlink ssh window".into()),
        Route::Unknown => Err("session ended".into()),
    }
}

/// A hook line from an agent whose prompts can only be answered by typing. Opens the card
/// in the background; returns at once.
pub fn on_event(env: &Env, v: &Value, key: &str) {
    if v["agent"].as_str() != Some("copilot") {
        return;
    }
    let Some(sid) = env.registry.sid_of(key) else {
        return;
    };
    let payload = &v["payload"];
    let event = v["event"].as_str().unwrap_or("").to_ascii_lowercase();
    let tool = payload["toolName"].as_str();
    let env = env.clone();
    let key = key.to_owned();
    match event.as_str() {
        "pretooluse" if tool == Some("ask_user") => {
            let steps = ask_requests(sid, key, &payload["toolArgs"]);
            tokio::spawn(async move {
                for (req, expect) in steps {
                    if !open_and_wait(env.clone(), sid, req, expect).await {
                        break;
                    }
                }
            });
        }
        "notification" if payload["notification_type"].as_str() == Some("permission_prompt") => {
            let message = payload["message"].as_str().unwrap_or("").to_owned();
            tokio::spawn(async move {
                let screen = wait_for_screen(&env, sid, Kind::Permission).await;
                let (req, expect) = permission_request(sid, key, &message, screen.as_ref());
                open_and_wait(env, sid, req, expect).await;
            });
        }
        _ => {}
    }
}

/// The notification can arrive a moment before the prompt is drawn.
async fn wait_for_screen(env: &Env, sid: u64, kind: Kind) -> Option<Screen> {
    for _ in 0..8 {
        let ctx = ctx_of(&env.registry, sid).ok()?;
        let term = env.term.clone();
        let got = tokio::task::spawn_blocking(move || term.read_screen(&ctx))
            .await
            .ok()?;
        match got {
            Ok(dump) => {
                if let Some(s) = parse(&dump).filter(|s| s.kind == kind) {
                    return Some(s);
                }
            }
            Err(_) => return None,
        }
        tokio::time::sleep(Duration::from_millis(250)).await;
    }
    None
}

/// Opens one card and types the device's answer; true when an answer was typed.
async fn open_and_wait(env: Env, sid: u64, req: Request, expect: Option<Expect>) -> bool {
    let ttl = req.ttl;
    let (rid, rx) = env.broker.open(req);
    let outcome: Outcome = match tokio::time::timeout(ttl, rx).await {
        Ok(Ok(o)) => o,
        Ok(Err(_)) => return false, // the terminal answered (or the card was cancelled)
        Err(_) => {
            env.broker.cancel(rid, ClearWhy::Expired);
            return false;
        }
    };
    let (Some(expect), Some(choice)) = (expect, outcome.choice) else {
        return false;
    };
    let res = match ctx_of(&env.registry, sid) {
        Ok(ctx) => {
            let term = env.term.clone();
            tokio::task::spawn_blocking(move || type_answer(&*term, &ctx, &expect, choice, KEY_GAP))
                .await
                .unwrap_or_else(|e| Err(e.to_string()))
        }
        Err(e) => Err(e),
    };
    if let Err(e) = &res {
        tracing::warn!("rid {rid}: answer not typed: {e}");
    }
    env.trace.audit(
        "decision",
        Some(sid),
        Some(rid),
        json!({"from": "device", "typed": res.is_ok(), "error": res.as_ref().err(), "choice": choice,
               "decision": outcome.decision}),
    );
    res.is_ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    const PERM: &str = "\
 ○ Shell Create file /tmp/x                                     18s
╭──────────────────────────────────────────────────────────────╮
│ Create file /tmp/x                                           │
│ ──────────────────────────────────────────────────────────── │
│ This command uses paths outside the allowed directories.     │
│                                                              │
│ ╭──────────────────────────────────────────────────────────╮ │
│ │ touch /tmp/x                                             │ │
│ ╰──────────────────────────────────────────────────────────╯ │
│                                                              │
│ Do you want to run this command?                             │
│                                                              │
│ ❯ 1. Yes                                                     │
│   2. Yes, and don't ask again for `touch` in this repo       │
│      (~/projects/m5-agent-link)                              │
│   3. No, and tell Copilot what to do differently (Esc to stop)│
│                                                              │
│ ↑/↓ to navigate · enter to select · esc to cancel            │
╰──────────────────────────────────────────────────────────────╯
";

    const FORM: &str = "\
 ○ Asking user Waiting for response
────────────────────────────────────────
 Copilot needs information.
 Please pick your favorite color.
 Color:
 ❯ red
   green
   blue
   Other (type your answer)
 ↑/↓ select · enter accept · ctrl+d decline · esc cancel
────────────────────────────────────────
";

    #[test]
    fn reads_copilots_permission_prompt() {
        let s = parse(PERM).unwrap();
        assert_eq!(s.kind, Kind::Permission);
        assert_eq!(s.opts.len(), 3);
        assert_eq!(s.opts[0], "Yes");
        assert!(s.opts[2].starts_with("No, and tell Copilot"));
        assert_eq!(s.cursor, Some(0));
        assert!(s.body.contains(&"touch /tmp/x".to_owned()));
        assert!(s
            .body
            .contains(&"Do you want to run this command?".to_owned()));
    }

    #[test]
    fn reads_copilots_choice_form() {
        let s = parse(FORM).unwrap();
        assert_eq!(s.kind, Kind::Choice);
        assert_eq!(s.opts, ["red", "green", "blue", OTHER]);
        assert_eq!(s.cursor, Some(0));
        assert_eq!(s.body.last().unwrap(), "Color:");
        // a boolean field (captured from v1.0.106)
        let yes_no = FORM
            .replace("Color:", "Restart the daemon after the fix?:")
            .replace("❯ red\n   green\n   blue", "❯ Yes\n   No");
        assert_eq!(parse(&yes_no).unwrap().opts, ["Yes", "No", OTHER]);
    }

    #[test]
    fn a_prompt_with_output_below_it_is_gone() {
        assert!(parse(&format!("{PERM} $ Shell Create file /tmp/x\n")).is_none());
        assert!(parse("● Done.\n").is_none());
    }

    #[test]
    fn permission_card_maps_the_screen_options() {
        let s = parse(PERM).unwrap();
        let (req, expect) =
            permission_request(4, "copilot:s".into(), "Run command: touch /tmp/x", Some(&s));
        assert_eq!(req.title.as_deref(), Some("Run command"));
        assert_eq!(req.text.as_deref(), Some("touch /tmp/x"));
        assert!(!req.focus_only);
        let labels: Vec<_> = req.opts.iter().map(|o| o.0.as_str()).collect();
        assert_eq!(labels, ["Yes", "Yes, don't ask again", "No"]);
        assert_eq!(
            req.opts.iter().map(|o| o.1.clone()).collect::<Vec<_>>(),
            [Meaning::Allow, Meaning::AllowGrant, Meaning::Deny]
        );
        assert!(matches!(expect, Some(Expect::Permission { n: 3, .. })));
        // unreadable terminal: Focus only
        let (req, expect) = permission_request(4, "copilot:s".into(), "Fetch it", None);
        assert!(req.focus_only && expect.is_none());
        assert_eq!(req.title.as_deref(), Some("Permission needed"));
    }

    fn one(args: &Value) -> (Request, Option<Expect>) {
        let mut steps = ask_requests(1, "k".into(), args);
        assert_eq!(steps.len(), 1);
        steps.pop().unwrap()
    }

    #[test]
    fn ask_user_cards() {
        let single = json!({"message": "Pick a color", "requestedSchema": {"properties": {
            "color": {"type": "string", "title": "Color", "enum": ["red", "green", "blue"]}}}});
        let (req, expect) = one(&single);
        assert_eq!(req.title.as_deref(), Some("Color"));
        assert_eq!(req.text.as_deref(), Some("Pick a color"));
        assert_eq!(req.opts.len(), 3);
        assert_eq!(
            expect,
            Some(Expect::Choice {
                labels: vec!["red".into(), "green".into(), "blue".into()],
                field: Some("Color".into()),
            })
        );
        let one_of = json!({"message": "m", "requestedSchema": {"properties": {"x": {"type": "string",
            "oneOf": [{"const": "a", "title": "Alpha"}, {"const": "b", "title": "Beta"}]}}}});
        assert_eq!(one(&one_of).0.opts[1].0, "Beta");
        let yes_no = json!({"message": "m", "requestedSchema": {"properties": {"ok": {"type": "boolean",
            "title": "Restart?", "default": true}}}});
        let (req, expect) = one(&yes_no);
        assert!(!req.focus_only);
        assert_eq!(req.title.as_deref(), Some("Restart?"));
        assert!(matches!(expect, Some(Expect::Choice { labels, .. }) if labels == ["Yes", "No"]));
        // a free-text field anywhere: one Focus-only card
        let mixed = json!({"message": "m", "requestedSchema": {"properties": {
            "a": {"type": "string", "enum": ["x"]}, "b": {"type": "string"}}}});
        let (req, expect) = one(&Value::String(mixed.to_string()));
        assert!(req.focus_only && expect.is_none());
        assert_eq!(req.text.as_deref(), Some("m"));
        let text =
            json!({"message": "m", "requestedSchema": {"properties": {"a": {"type": "string"}}}});
        assert!(one(&text).0.focus_only);
    }

    #[test]
    fn ask_user_fields_become_cards_in_schema_order() {
        // keys deliberately not in alphabetical order, as a JSON string like the hook's
        let form = r#"{"message": "Sample", "requestedSchema": {"properties": {
            "color": {"type": "string", "title": "Pick a color", "enum": ["Red", "Green"]},
            "again": {"type": "boolean", "title": "Run again?"}}}}"#;
        let steps = ask_requests(1, "k".into(), &Value::String(form.into()));
        let titles: Vec<_> = steps.iter().map(|s| s.0.title.clone().unwrap()).collect();
        assert_eq!(titles, ["Pick a color", "Run again?"]);
        assert_eq!(steps[0].0.text.as_deref(), Some("(1/2) Sample"));
        assert_eq!(steps[1].0.text.as_deref(), Some("(2/2) Sample"));
        assert!(steps.iter().all(|s| !s.0.focus_only && s.1.is_some()));
    }

    /// A screen that changes with each key, and the keys it got.
    struct Fake {
        screens: Mutex<Vec<String>>,
        keys: Mutex<Vec<String>>,
    }
    impl Fake {
        fn new(screens: &[&str]) -> Self {
            Fake {
                screens: Mutex::new(screens.iter().rev().map(|s| s.to_string()).collect()),
                keys: Mutex::new(Vec::new()),
            }
        }
    }
    impl Terminal for Fake {
        fn read_screen(&self, _: &HostCtx) -> Result<String, FocusError> {
            let mut s = self.screens.lock().unwrap();
            if s.len() > 1 {
                Ok(s.pop().unwrap())
            } else {
                Ok(s[0].clone())
            }
        }
        fn send_keys(&self, _: &HostCtx, k: &str) -> Result<(), FocusError> {
            self.keys.lock().unwrap().push(k.to_owned());
            Ok(())
        }
    }

    #[test]
    fn types_the_digit_only_for_the_same_prompt() {
        let s = parse(PERM).unwrap();
        let (_, expect) = permission_request(1, "k".into(), "x", Some(&s));
        let expect = expect.unwrap();
        let t = Fake::new(&[PERM]);
        type_answer(&t, &HostCtx::default(), &expect, 2, Duration::ZERO).unwrap();
        assert_eq!(*t.keys.lock().unwrap(), ["3"]);

        let other = PERM.replace("touch /tmp/x  ", "rm -rf /tmp/x");
        let t = Fake::new(&[&other]);
        assert!(type_answer(&t, &HostCtx::default(), &expect, 0, Duration::ZERO).is_err());
        let t = Fake::new(&["● Done.\n"]);
        assert!(type_answer(&t, &HostCtx::default(), &expect, 0, Duration::ZERO).is_err());
        assert!(t.keys.lock().unwrap().is_empty());
    }

    #[test]
    fn moves_the_cursor_checks_it_then_accepts() {
        let expect = Expect::Choice {
            labels: vec!["red".into(), "green".into(), "blue".into()],
            field: Some("Color".into()),
        };
        let moved = FORM.replace("❯ red", "  red").replace("  blue", "❯ blue");
        let t = Fake::new(&[FORM, &moved]);
        type_answer(&t, &HostCtx::default(), &expect, 2, Duration::ZERO).unwrap();
        assert_eq!(*t.keys.lock().unwrap(), [DOWN, DOWN, ENTER]);

        // the cursor did not land: no Enter
        let t = Fake::new(&[FORM, FORM]);
        assert!(type_answer(&t, &HostCtx::default(), &expect, 1, Duration::ZERO).is_err());
        assert_eq!(*t.keys.lock().unwrap(), [DOWN]);

        let wrong = Expect::Choice {
            labels: vec!["yes".into(), "no".into()],
            field: None,
        };
        let t = Fake::new(&[FORM]);
        assert!(type_answer(&t, &HostCtx::default(), &wrong, 0, Duration::ZERO).is_err());
    }

    const TABS: &str = "\
 ○ Asking user Waiting for response
────────────────────────────────────────
 Copilot needs information.
 Sample form.
  Pick a color   Run again?
 Run again?:
   Yes
 ❯ No
   Other (type your answer)
 ↑/↓ select · enter accept · shift+tab prev · ctrl+d decline · esc cancel
────────────────────────────────────────
";

    #[test]
    fn answers_only_the_field_on_screen() {
        let again = Expect::Choice {
            labels: vec!["Yes".into(), "No".into()],
            field: Some("Run again?".into()),
        };
        let up = TABS.replace("   Yes", " ❯ Yes").replace(" ❯ No", "   No");
        let t = Fake::new(&[TABS, &up]);
        type_answer(&t, &HostCtx::default(), &again, 0, Duration::ZERO).unwrap();
        assert_eq!(*t.keys.lock().unwrap(), [UP, ENTER]);

        // same choices, but the screen is on another field: nothing typed
        let other = Expect::Choice {
            labels: vec!["Yes".into(), "No".into()],
            field: Some("Restart?".into()),
        };
        let t = Fake::new(&[TABS]);
        assert!(type_answer(&t, &HostCtx::default(), &other, 1, Duration::ZERO).is_err());
        assert!(t.keys.lock().unwrap().is_empty());
    }
}
