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
//!   and not in schema order; Enter accepts the field and moves to the next, and submits on
//!   the last. A free-text field is its title (no colon) above an input box (`▄` rule, `❯ `
//!   and what was typed, `▀` rule) and `enter accept · … · ctrl+d decline · esc cancel`.

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
    /// A form's free-text field.
    Text,
}

/// The prompt at the bottom of a screen dump.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Screen {
    pub kind: Kind,
    /// The prompt's text above its options; identifies *this* prompt.
    pub body: Vec<String>,
    pub opts: Vec<String>,
    pub cursor: Option<usize>,
    /// What a free-text field's box holds.
    pub input: String,
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
    /// A free-text field, answered by dictation; `field` as for `Choice`.
    Text { field: Option<String> },
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
        (l.starts_with("↑/↓") && (l.contains("enter to select") || l.contains("enter accept")))
            || (l.starts_with("enter accept") && l.contains("ctrl+d decline"))
    })?;
    // Anything but blank lines and box edges below the footer means the prompt is gone.
    if lines[f + 1..].iter().any(|l| !l.is_empty() && !is_rule(l)) {
        return None;
    }
    if lines[f].contains("enter to select") {
        parse_permission(&raw, &lines, f)
    } else if lines[f].starts_with("enter accept") {
        parse_text(&lines, f)
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
        input: String::new(),
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
    Some(Screen {
        kind: Kind::Choice,
        body: body_above(lines, i),
        opts,
        cursor,
        input: String::new(),
    })
}

/// The body ends at a rule; `from` is the line above the field's own content.
fn body_above(lines: &[&str], from: usize) -> Vec<String> {
    let mut body = Vec::new();
    let mut j = from + 1;
    while j > 0 && from + 1 - j < 12 {
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
    body
}

fn parse_text(lines: &[&str], f: usize) -> Option<Screen> {
    // up from the footer: the box's bottom edge, its lines, its top edge
    let mut i = (0..f).rev().find(|&j| !lines[j].is_empty())?;
    if !lines[i].starts_with('▀') {
        return None;
    }
    let mut boxed = Vec::new();
    loop {
        if i == 0 || f - i > 12 {
            return None;
        }
        i -= 1;
        if lines[i].starts_with('▄') {
            break;
        }
        boxed.push(lines[i]);
    }
    boxed.reverse();
    let first = boxed.first()?.strip_prefix('❯')?;
    let input = std::iter::once(first)
        .chain(boxed[1..].iter().copied())
        .map(str::trim)
        .filter(|l| !l.is_empty())
        .collect::<Vec<_>>()
        .join(" ");
    let body = body_above(lines, i.checked_sub(1)?);
    if body.is_empty() {
        return None;
    }
    Some(Screen {
        kind: Kind::Text,
        body,
        opts: Vec::new(),
        cursor: None,
        input,
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
        dictate: false,
        notice: false,
        standing: None,
        questions: Vec::new(),
    };
    let Some(s) = screen.filter(|s| s.kind == Kind::Permission && s.opts.len() <= 9) else {
        return (req, None);
    };
    // The first option (allow once) and the "No" ones. Copilot's "Yes, and don't ask again for `touch` in
    // this repo" and any other allow-class option are left to the terminal: what they save is read off
    // the screen, not known exactly, so the watch cannot show the rule it would grant
    // (STANDING-RULES.md §7). Four fit on the watch.
    let is_no = |i: usize| s.opts[i].to_ascii_lowercase().starts_with("no");
    let map: Vec<usize> = (0..s.opts.len())
        .filter(|&i| i == 0 || is_no(i))
        .take(4)
        .collect();
    req.opts = map
        .iter()
        .map(|&i| {
            let meaning = if is_no(i) {
                Meaning::Deny
            } else {
                Meaning::Allow
            };
            (short_label(i, &s.opts[i]), meaning)
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

/// A field without fixed choices that is typed as text (string or number); anything else
/// (multi-select arrays, objects) is not answered from the device.
fn is_text_field(field: &Value) -> bool {
    matches!(
        field["type"].as_str(),
        None | Some("string") | Some("number") | Some("integer")
    )
}

/// Cards and screen expectations for Copilot's `ask_user` (`toolArgs`: `message` +
/// `requestedSchema`): one card per field, a field with fixed choices answered by picking one
/// and a free-text field by dictation (or on the PC); a form with any other kind of field is
/// a single Focus-only card. Copilot does not draw the fields in schema order, so with several
/// fields each card names its field (title, else key) and is matched to the screen.
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
        dictate: false,
        notice: false,
        standing: None,
        questions: Vec::new(),
    };
    let fields: Vec<(Option<String>, Vec<String>, bool)> = args["requestedSchema"]["properties"]
        .as_object()
        .map(|p| {
            p.iter()
                .map(|(k, f)| {
                    let title = f["title"].as_str().filter(|t| !t.is_empty());
                    let name = match title {
                        Some(t) => Some(t.to_owned()),
                        None if p.len() > 1 => Some(k.clone()),
                        None => None,
                    };
                    let labels = field_labels(f);
                    let text = labels.is_empty() && is_text_field(f);
                    (name, labels, text)
                })
                .collect()
        })
        .unwrap_or_default();
    if fields.is_empty() || fields.iter().any(|(_, l, text)| l.is_empty() && !text) {
        if fields.len() == 1 {
            let mut req = base;
            if let Some(t) = &fields[0].0 {
                req.title = Some(t.clone());
            }
            return vec![(req, None)];
        }
        return vec![(base, None)];
    }
    fields
        .into_iter()
        .map(|(title, labels, text)| {
            let labels: Vec<String> = labels.into_iter().take(4).collect();
            let mut req = base.clone();
            if let Some(t) = &title {
                req.title = Some(t.clone());
            }
            if text {
                req.focus_only = false;
                req.dictate = true;
                return (req, Some(Expect::Text { field: title }));
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

/// The field title a form screen shows: `Title:` above choices, `Title` above a text box.
fn field_shown(s: &Screen) -> Option<&str> {
    let last = s.body.last()?;
    match s.kind {
        Kind::Choice => last.strip_suffix(':'),
        Kind::Text => Some(last.as_str()),
        Kind::Permission => None,
    }
}

fn field_ok(s: &Screen, field: &Option<String>) -> bool {
    field
        .as_ref()
        .is_none_or(|f| field_shown(s).is_some_and(|l| same_label(l, f)))
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
            if s.kind != Kind::Choice
                || !field_ok(&s, field)
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
        Expect::Text { .. } => Err("a text field takes a dictated answer".into()),
    }
}

/// How much of a typed answer the box must show before Enter: enough to tell it is ours.
const TEXT_CHECK: usize = 16;

/// Types a dictated answer into the free-text field `field`, if the screen still shows that
/// field with an empty box, then checks the box shows it before pressing Enter.
pub fn type_text(
    term: &dyn Terminal,
    ctx: &HostCtx,
    field: &Option<String>,
    text: &str,
    gap: Duration,
) -> Result<(), String> {
    let text = crate::broker::tidy(text);
    if text.is_empty() {
        return Err("nothing was heard".into());
    }
    let s = read(term, ctx)?;
    if s.kind != Kind::Text || !field_ok(&s, field) {
        return Err("a different prompt is on screen".into());
    }
    if !s.input.is_empty() {
        return Err("the field already holds text".into());
    }
    term.send_keys(ctx, &crate::typing::escape(&text))
        .map_err(err)?;
    let want: String = text.chars().take(TEXT_CHECK).collect();
    // the TUI draws what was typed a moment later
    for _ in 0..5 {
        std::thread::sleep(gap);
        let s = read(term, ctx)?;
        if s.kind == Kind::Text && field_ok(&s, field) && s.input.starts_with(want.trim_end()) {
            return term.send_keys(ctx, ENTER).map_err(err);
        }
    }
    Err("the answer did not show in the field".into())
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
            tokio::spawn(async move { ask_in_turn(env, sid, steps).await });
        }
        "notification" if payload["notification_type"].as_str() == Some("permission_prompt") => {
            let message = payload["message"].as_str().unwrap_or("").to_owned();
            tokio::spawn(async move {
                let screen = wait_for_screen(&env, sid, &[Kind::Permission]).await;
                let (req, expect) = permission_request(sid, key, &message, screen.as_ref());
                open_and_wait(env, sid, req, expect).await;
            });
        }
        _ => {}
    }
}

/// The not-yet-answered card whose field is on screen.
fn step_on_screen(screen: &Screen, steps: &[(Request, Option<Expect>)]) -> Option<usize> {
    let shown = field_shown(screen)?;
    steps.iter().position(|(_, e)| match (e, screen.kind) {
        (Some(Expect::Choice { field: Some(f), .. }), Kind::Choice)
        | (Some(Expect::Text { field: Some(f) }), Kind::Text) => same_label(shown, f),
        _ => false,
    })
}

/// Opens the `ask_user` cards one at a time: each for the field the screen shows next, after
/// the previous answer was typed. A field the screen check can't place ends the run there.
async fn ask_in_turn(env: Env, sid: u64, mut steps: Vec<(Request, Option<Expect>)>) {
    if steps.len() == 1 {
        let (req, expect) = steps.pop().unwrap();
        open_and_wait(env, sid, req, expect).await;
        return;
    }
    let n = steps.len();
    for k in 1..=n {
        // just after an Enter the previous field can still be drawn
        let mut found = None;
        for _ in 0..4 {
            let Some(screen) = wait_for_screen(&env, sid, &[Kind::Choice, Kind::Text]).await else {
                return;
            };
            found = step_on_screen(&screen, &steps);
            if found.is_some() {
                break;
            }
            tokio::time::sleep(Duration::from_millis(250)).await;
        }
        let Some(i) = found else {
            tracing::warn!("ask_user: the field on screen is not one a card expects");
            return;
        };
        let (mut req, expect) = steps.remove(i);
        let text = req.text.take().unwrap_or_default();
        req.text = Some(format!("({k}/{n}) {text}").trim_end().to_owned());
        if !open_and_wait(env.clone(), sid, req, expect).await {
            return;
        }
    }
}

/// The notification can arrive a moment before the prompt is drawn.
async fn wait_for_screen(env: &Env, sid: u64, kinds: &[Kind]) -> Option<Screen> {
    for _ in 0..8 {
        let ctx = ctx_of(&env.registry, sid).ok()?;
        let term = env.term.clone();
        let got = tokio::task::spawn_blocking(move || term.read_screen(&ctx))
            .await
            .ok()?;
        match got {
            Ok(dump) => {
                if let Some(s) = parse(&dump).filter(|s| kinds.contains(&s.kind)) {
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
    use crate::presence::{global, hush, Event};
    let (here, _, _) = global().here_async(env.registry.host_ctx(sid)).await;
    let (rid, rx) = env.broker.open_with(req, hush(Event::Needs, here));
    let outcome: Outcome = match tokio::time::timeout(ttl, rx).await {
        Ok(Ok(o)) => o,
        Ok(Err(_)) => return false, // the terminal answered (or the card was cancelled)
        Err(_) => {
            env.broker.cancel(rid, ClearWhy::Expired);
            return false;
        }
    };
    let Some(expect) = expect else {
        return false;
    };
    let choice = outcome.choice;
    // a dictated answer is the user's own words: typed, never traced
    let text = outcome
        .answer
        .filter(|_| matches!(expect, Expect::Text { .. }));
    if choice.is_none() && text.is_none() {
        return false;
    }
    let res = match ctx_of(&env.registry, sid) {
        Ok(ctx) => {
            let term = env.term.clone();
            tokio::task::spawn_blocking(move || match (&expect, text, choice) {
                (Expect::Text { field }, Some(t), _) => type_text(&*term, &ctx, field, &t, KEY_GAP),
                (_, _, Some(c)) => type_answer(&*term, &ctx, &expect, c, KEY_GAP),
                _ => Err("no answer to type".into()),
            })
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
        // the "don't ask again" option is left to the terminal (STANDING-RULES.md §7)
        assert_eq!(labels, ["Yes", "No"]);
        assert_eq!(
            req.opts.iter().map(|o| o.1.clone()).collect::<Vec<_>>(),
            [Meaning::Allow, Meaning::Deny]
        );
        match expect {
            Some(Expect::Permission { n: 3, ref map, .. }) => assert_eq!(map, &[0, 2]),
            ref e => panic!("{e:?}"),
        }
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
        // free text: a card answered by dictation, no options
        let text = json!({"message": "m", "requestedSchema": {"properties": {
            "a": {"type": "string", "title": "Your note"}}}});
        let (req, expect) = one(&text);
        assert!(req.dictate && !req.focus_only && req.opts.is_empty());
        assert_eq!(req.title.as_deref(), Some("Your note"));
        assert_eq!(
            expect,
            Some(Expect::Text {
                field: Some("Your note".into())
            })
        );
        // a field the device cannot answer anywhere (multi-select): one Focus-only card
        let multi = json!({"message": "m", "requestedSchema": {"properties": {
            "a": {"type": "string", "enum": ["x"]},
            "b": {"type": "array", "items": {"enum": ["p", "q"]}}}}});
        let (req, expect) = one(&Value::String(multi.to_string()));
        assert!(req.focus_only && !req.dictate && expect.is_none());
        assert_eq!(req.text.as_deref(), Some("m"));
    }

    #[test]
    fn ask_user_fields_become_cards_matched_to_the_screen() {
        let form = r#"{"message": "Sample", "requestedSchema": {"properties": {
            "again": {"type": "boolean", "title": "Run again?"},
            "color": {"type": "string", "enum": ["Red", "Green"]}}}}"#;
        let steps = ask_requests(1, "k".into(), &Value::String(form.into()));
        assert_eq!(steps.len(), 2);
        assert!(steps.iter().all(|s| !s.0.focus_only));
        // Copilot may draw "color" first: the card for the field on screen is picked, by
        // title, or by key when the field has none
        let color = TABS
            .replace(" Run again?:", " color:")
            .replace("   Yes\n ❯ No", " ❯ Red\n   Green");
        assert_eq!(step_on_screen(&parse(&color).unwrap(), &steps), Some(1));
        assert_eq!(step_on_screen(&parse(TABS).unwrap(), &steps), Some(0));
        assert_eq!(step_on_screen(&parse(FORM).unwrap(), &steps), None);
    }

    /// As Copilot draws a free-text field (captured from v1.0.10x).
    const TEXT: &str = "\
 ○ Asking user Waiting for response
────────────────────────────────────────
 Copilot needs information.
 Tell me about it.
  Your note   Pick a size
 Your note
 ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
  ❯
 ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
 enter accept · tab next · ctrl+d decline · esc cancel
────────────────────────────────────────
";

    #[test]
    fn reads_a_free_text_field() {
        let s = parse(TEXT).unwrap();
        assert_eq!(s.kind, Kind::Text);
        assert_eq!(field_shown(&s), Some("Your note"));
        assert_eq!(s.input, "");
        let typed = TEXT.replace("  ❯\n", "  ❯ fix the login bug\n");
        assert_eq!(parse(&typed).unwrap().input, "fix the login bug");
        // a mixed form: the text card goes with the text field, the choice card with the list
        let form = json!({"message": "m", "requestedSchema": {"properties": {
            "note": {"type": "string", "title": "Your note"},
            "size": {"type": "string", "title": "Pick a size", "enum": ["Small", "Large"]}}}});
        let steps = ask_requests(1, "k".into(), &form);
        assert_eq!(step_on_screen(&s, &steps), Some(0));
        let size = TABS
            .replace(" Run again?:", " Pick a size:")
            .replace("   Yes\n ❯ No", " ❯ Small\n   Large");
        assert_eq!(step_on_screen(&parse(&size).unwrap(), &steps), Some(1));
    }

    #[test]
    fn types_a_dictated_answer_checks_it_then_accepts() {
        let note = Some("Your note".to_owned());
        let typed = TEXT.replace("  ❯\n", "  ❯ fix the login bug\n");
        let t = Fake::new(&[TEXT, &typed]);
        type_text(
            &t,
            &HostCtx::default(),
            &note,
            " fix the\nlogin bug ",
            Duration::ZERO,
        )
        .unwrap();
        assert_eq!(*t.keys.lock().unwrap(), ["fix the login bug", ENTER]);

        // the text never showed: no Enter
        let t = Fake::new(&[TEXT, TEXT]);
        assert!(type_text(&t, &HostCtx::default(), &note, "hello", Duration::ZERO).is_err());
        assert_eq!(*t.keys.lock().unwrap(), ["hello"]);

        // another field, a filled box, or a choice list on screen: nothing typed
        for screen in [
            TEXT.replace(" Your note\n", " Other\n"),
            typed,
            FORM.to_owned(),
        ] {
            let t = Fake::new(&[&screen]);
            assert!(type_text(&t, &HostCtx::default(), &note, "hello", Duration::ZERO).is_err());
            assert!(t.keys.lock().unwrap().is_empty());
        }
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
        // the watch's second option is the "No", the screen's third
        type_answer(&t, &HostCtx::default(), &expect, 1, Duration::ZERO).unwrap();
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
