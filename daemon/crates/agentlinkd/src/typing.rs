//! Input from the watch typed into an agent's terminal (issue 20261110-b150): quick replies
//! (`CmdOp::Type`, an index into the list the welcome carried) and interrupt (`CmdOp::Cancel`,
//! Esc). The device never sends text: it names a reply the daemon already holds.
//!
//! Typing is refused while the session could be showing a choice the keys would answer. Enter on
//! a Claude Code permission dialog selects its highlighted option ("Yes"), so: no open request in
//! the broker, the session is not waiting on an approval or a question, and no numbered selection
//! list is on screen. Only kitty can be typed into today (`Terminal`).

use std::path::Path;

use agentlink_proto::{InputSub, SessState, Session};

use crate::focus::{FocusError, Terminal};
use crate::registry::{clip, HostCtx};

pub const MAX_REPLIES: usize = 6;
pub const MAX_REPLY_CHARS: usize = 24;
/// kitty's `send-text` interprets Python-style escapes.
const ENTER: &str = "\\r";
const ESC: &str = "\\x1b";
const DEL: &str = "\\x7f";

/// The user's fixed quick replies: `replies.txt` (one per line, `#` comments), else none. There are
/// no built-in defaults: replies that fit no question ("Continue", "Yes") read as noise on the watch,
/// whose Reply list leads with the answers to the agent's own closing question (ask.rs). Control
/// characters are dropped, each is cut at 24 chars, at most six.
pub fn load_replies(dir: &Path) -> Vec<String> {
    std::fs::read_to_string(dir.join("replies.txt"))
        .map(|text| parse_replies(&text))
        .unwrap_or_default()
}

pub fn parse_replies(text: &str) -> Vec<String> {
    text.lines()
        .map(|l| l.trim())
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(|l| {
            let clean: String = l.chars().filter(|c| !c.is_control()).collect();
            clip(clean.trim(), MAX_REPLY_CHARS)
        })
        .filter(|l| !l.is_empty())
        .take(MAX_REPLIES)
        .collect()
}

/// `text` for kitty's `send-text`: a backslash would start an escape.
fn escape(text: &str) -> String {
    text.replace('\\', "\\\\")
}

/// Why the session must not be typed into now, if anything.
pub fn refusal(session: &Session, request_open: bool) -> Option<&'static str> {
    if request_open {
        return Some("Answer the request first");
    }
    if session.st == Some(SessState::Input)
        && matches!(
            session.sub,
            Some(InputSub::Approval) | Some(InputSub::Question)
        )
    {
        return Some("Answer the agent first");
    }
    None
}

/// A numbered selection list with a cursor ("❯ 1. Yes"): Claude Code and Copilot dialogs both
/// look like this, and Enter would pick the highlighted option.
fn choice_on_screen(dump: &str) -> bool {
    dump.lines().any(|l| {
        let t = l.trim_start_matches(|c: char| c.is_whitespace() || c == '│');
        let rest = t
            .strip_prefix('❯')
            .or_else(|| t.strip_prefix('>'))
            .or_else(|| t.strip_prefix('›'));
        rest.is_some_and(|r| {
            let r = r.trim_start();
            r.chars().next().is_some_and(|c| c.is_ascii_digit()) && r.contains(". ")
        })
    })
}

fn err(e: FocusError) -> String {
    match e {
        FocusError::NoWindow => "no window found".into(),
        FocusError::Unsupported => "this terminal cannot be typed into".into(),
        FocusError::Internal(m) => m,
    }
}

/// Types `text` and Enter, unless a selection list is on screen.
pub fn type_reply(term: &dyn Terminal, ctx: &HostCtx, text: &str) -> Result<(), String> {
    let screen = term.read_screen(ctx).map_err(err)?;
    if choice_on_screen(&screen) {
        return Err("A prompt is on screen".into());
    }
    term.send_keys(ctx, &escape(text)).map_err(err)?;
    term.send_keys(ctx, ENTER).map_err(err)
}

/// Keyboard composition (keys.rs): `erase` backspaces, then `text`, no Enter. The same screen check
/// as a quick reply.
pub fn edit_text(
    term: &dyn Terminal,
    ctx: &HostCtx,
    erase: usize,
    text: &str,
) -> Result<(), String> {
    let screen = term.read_screen(ctx).map_err(err)?;
    if choice_on_screen(&screen) {
        return Err("A prompt is on screen".into());
    }
    if erase > 0 {
        term.send_keys(ctx, &DEL.repeat(erase)).map_err(err)?;
    }
    if !text.is_empty() {
        term.send_keys(ctx, &escape(text)).map_err(err)?;
    }
    Ok(())
}

/// Enter alone, for `kb_send`: the composed words are already in the prompt.
pub fn press_enter(term: &dyn Terminal, ctx: &HostCtx) -> Result<(), String> {
    let screen = term.read_screen(ctx).map_err(err)?;
    if choice_on_screen(&screen) {
        return Err("A prompt is on screen".into());
    }
    term.send_keys(ctx, ENTER).map_err(err)
}

/// Esc: Claude Code and Copilot CLI both stop the running turn on it.
pub fn interrupt(term: &dyn Terminal, ctx: &HostCtx) -> Result<(), String> {
    term.send_keys(ctx, ESC).map_err(err)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    #[derive(Default)]
    struct Fake {
        screen: String,
        sent: Mutex<Vec<String>>,
    }
    impl Terminal for Fake {
        fn read_screen(&self, _: &HostCtx) -> Result<String, FocusError> {
            Ok(self.screen.clone())
        }
        fn send_keys(&self, _: &HostCtx, k: &str) -> Result<(), FocusError> {
            self.sent.lock().unwrap().push(k.to_owned());
            Ok(())
        }
    }

    #[test]
    fn replies_file_is_cleaned_and_bounded() {
        let r = parse_replies("# mine\nContinue\n\n  Yes, go on  \nfix the \x07bell\nA reply that is far too long to fit\n4\n5\n6\n7\n");
        assert_eq!(r.len(), MAX_REPLIES);
        assert_eq!(r[0], "Continue");
        assert_eq!(r[1], "Yes, go on");
        assert_eq!(r[2], "fix the bell");
        assert!(r[3].chars().count() <= MAX_REPLY_CHARS);
    }

    #[test]
    fn missing_file_gives_no_fixed_replies() {
        let dir = std::env::temp_dir().join("agentlink-no-replies-here");
        assert!(load_replies(&dir).is_empty());
    }

    #[test]
    fn types_text_then_enter_with_backslashes_escaped() {
        let t = Fake::default();
        type_reply(&t, &HostCtx::default(), "use C:\\tmp").unwrap();
        assert_eq!(*t.sent.lock().unwrap(), vec!["use C:\\\\tmp", "\\r"]);
    }

    #[test]
    fn never_types_into_a_selection_dialog() {
        for screen in [
            " Do you want to proceed?\n ❯ 1. Yes\n   2. No, and tell Claude what to do differently (esc)\n",
            "│ > 1. Yes                │\n│   2. No                 │",
        ] {
            let t = Fake { screen: screen.into(), ..Default::default() };
            assert!(type_reply(&t, &HostCtx::default(), "Continue").is_err(), "{screen}");
            assert!(t.sent.lock().unwrap().is_empty(), "nothing typed");
        }
        let plain = Fake {
            screen: "> \n  ? for shortcuts\n".into(),
            ..Default::default()
        };
        assert!(type_reply(&plain, &HostCtx::default(), "Continue").is_ok());
    }

    #[test]
    fn refuses_while_the_agent_asks() {
        let mut s = Session::delta(3);
        s.st = Some(SessState::Input);
        s.sub = Some(InputSub::Approval);
        assert!(refusal(&s, false).is_some());
        s.sub = Some(InputSub::Question);
        assert!(refusal(&s, false).is_some());
        s.sub = None;
        assert!(
            refusal(&s, false).is_none(),
            "waiting for the next prompt: fine"
        );
        assert!(refusal(&s, true).is_some(), "an open request always blocks");
        s.st = Some(SessState::Done);
        assert!(refusal(&s, false).is_none());
    }

    #[test]
    fn interrupt_is_one_escape() {
        let t = Fake::default();
        interrupt(&t, &HostCtx::default()).unwrap();
        assert_eq!(*t.sent.lock().unwrap(), vec!["\\x1b"]);
    }
}
