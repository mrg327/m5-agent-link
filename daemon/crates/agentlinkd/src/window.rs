//! The answer window (docs/design/ANSWER-WINDOW.md): how long a blocking Claude Code hook holds the
//! terminal's own prompt back while the watch shows the request.
//!
//! Claude Code runs the `PermissionRequest` hook *before* it shows its permission dialog, and a
//! synchronous hook blocks until it returns; there is no supported way to have the dialog and the
//! hook live at the same time. So the window is a trade: long enough to reach the watch from another
//! room, short enough that a user at the keyboard is not kept waiting for nothing. Silence never
//! decides: when the window ends the hook prints nothing and the terminal asks as usual.
//!
//! [`answer_window`] is the one place that decides the window for a request. Presence (PRESENCE.md) acts
//! here and nowhere else: with the user at the keyboard the default window shortens to the table's cell;
//! a window the user set explicitly (setting or the hook's own pin) is never touched by it.

use crate::presence::{cell, Cell, Event, Here};
use serde_json::Value;
use std::path::{Path, PathBuf};
use std::time::Duration;

/// Default window: the arrival buzz and three reminders 15 s apart (UI-V2 §4.3) all fall inside it,
/// and a user at the keyboard waits at most a minute (less with Open on PC on the watch).
pub const DEFAULT: Duration = Duration::from_secs(60);
/// Shortest window a setting can ask for (0 is allowed separately: "never hold").
pub const MIN: Duration = Duration::from_secs(5);
/// Longest: five minutes, half of Claude Code's 600 s default hook timeout.
pub const MAX: Duration = Duration::from_secs(300);
/// With no watch linked nobody can answer; this covers a link that is just reconnecting.
pub const NO_DEVICE: Duration = Duration::from_secs(10);
/// Kept free below the hook's own Claude Code `timeout`, so the daemon's "no decision" arrives
/// before Claude Code kills the hook.
pub const HOOK_MARGIN: Duration = Duration::from_secs(3);
/// Claude Code's default timeout for a command hook without its own `timeout` (hooks reference).
pub const CLAUDE_HOOK_DEFAULT: Duration = Duration::from_secs(600);

/// What the daemon knows about a request when it decides the window.
#[derive(Debug, Clone, Default)]
pub struct Ask<'a> {
    /// Claude Code hook event (`PermissionRequest`, `PreToolUse`).
    pub event: &'a str,
    /// The agent's working directory (`payload.cwd`), for its project settings.
    pub cwd: Option<&'a str>,
    /// The hook came from another host (REMOTE.md): its settings are not readable here.
    pub remote: bool,
    /// The hook's own `AGENTLINK_WAIT_MS`, when the user set one there: it wins over the daemon's.
    pub pinned: Option<Duration>,
    /// Devices linked right now.
    pub devices: usize,
    /// Where the user is relative to this session's terminal (presence.rs); `Away` = as before.
    pub here: Here,
}

/// The window for one request. 0 = do not hold at all (the terminal asks at once).
pub fn answer_window(ask: &Ask) -> Duration {
    let configured = ask.pinned.or_else(configured);
    let hook_timeout = if ask.remote {
        None
    } else {
        hook_timeout(&settings_files(ask.cwd), ask.event)
    };
    decide(configured, ask.here, hook_timeout, ask.devices)
}

/// The pure rule behind [`answer_window`].
pub fn decide(
    configured: Option<Duration>,
    here: Here,
    hook_timeout: Option<Duration>,
    devices: usize,
) -> Duration {
    let mut w = match configured {
        Some(d) if d.is_zero() => return Duration::ZERO,
        // an explicit setting wins over presence
        Some(d) => d.clamp(MIN, MAX),
        None => match cell(Event::Window, here) {
            Cell::Short(d) => d.min(DEFAULT),
            _ => DEFAULT,
        },
    };
    if let Some(t) = hook_timeout {
        // Claude Code kills the hook at its timeout and then shows the dialog anyway; the watch's
        // countdown must not promise more time than that.
        w = w.min(t.saturating_sub(HOOK_MARGIN));
    }
    if devices == 0 {
        w = w.min(NO_DEVICE);
    }
    w
}

/// The daemon's setting: `AGENTLINK_ANSWER_WINDOW_S`, else the first number in
/// `~/.agentlink/answer_window` (seconds; 0 = never hold). Read per request, so an edit applies to
/// the next one without a restart.
pub fn configured() -> Option<Duration> {
    let from_env = std::env::var("AGENTLINK_ANSWER_WINDOW_S").ok();
    let from_file = || std::fs::read_to_string(crate::paths::home().join("answer_window")).ok();
    parse_secs(from_env.as_deref()).or_else(|| parse_secs(from_file().as_deref()))
}

fn parse_secs(s: Option<&str>) -> Option<Duration> {
    let s = s?;
    let line = s
        .lines()
        .map(str::trim)
        .find(|l| !l.is_empty() && !l.starts_with('#'))?;
    let secs: f64 = line.split_whitespace().next()?.parse().ok()?;
    (secs.is_finite() && secs >= 0.0).then(|| Duration::from_millis((secs * 1000.0) as u64))
}

/// Claude Code's settings files that can hold hooks for a session in `cwd`: the user's, the
/// project's and the project's local ones.
fn settings_files(cwd: Option<&str>) -> Vec<PathBuf> {
    let user_dir = std::env::var_os("CLAUDE_CONFIG_DIR")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".claude")));
    let mut v = Vec::new();
    if let Some(d) = user_dir {
        v.push(d.join("settings.json"));
    }
    if let Some(cwd) = cwd.filter(|c| !c.is_empty()) {
        let d = Path::new(cwd).join(".claude");
        v.push(d.join("settings.json"));
        v.push(d.join("settings.local.json"));
    }
    v
}

/// The shortest Claude Code `timeout` of an `agentlink-hook` entry for `event` in `files`
/// (Claude Code's default 600 s for an entry without one); `None` when none names it.
pub fn hook_timeout(files: &[PathBuf], event: &str) -> Option<Duration> {
    files
        .iter()
        .filter_map(|f| std::fs::read(f).ok())
        .filter_map(|b| serde_json::from_slice::<Value>(&b).ok())
        .filter_map(|v| timeout_in(&v, event))
        .min()
}

fn timeout_in(settings: &Value, event: &str) -> Option<Duration> {
    settings["hooks"][event]
        .as_array()?
        .iter()
        .filter_map(|m| m["hooks"].as_array())
        .flatten()
        .filter(|h| {
            h["command"]
                .as_str()
                .is_some_and(|c| c.contains("agentlink-hook"))
        })
        .map(|h| {
            h["timeout"]
                .as_f64()
                .filter(|t| t.is_finite() && *t > 0.0)
                .map_or(CLAUDE_HOOK_DEFAULT, |t| {
                    Duration::from_millis((t * 1000.0) as u64)
                })
        })
        .min()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    const S: fn(u64) -> Duration = Duration::from_secs;

    #[test]
    fn the_default_is_a_minute_and_settings_are_clamped() {
        assert_eq!(decide(None, Here::Away, None, 1), S(60));
        assert_eq!(
            decide(Some(S(1)), Here::Away, None, 1),
            MIN,
            "too short to reach the watch"
        );
        assert_eq!(decide(Some(S(3600)), Here::Away, None, 1), MAX);
        assert_eq!(decide(Some(S(90)), Here::Away, None, 1), S(90));
        assert_eq!(
            decide(Some(Duration::ZERO), Here::Away, Some(S(30)), 1),
            Duration::ZERO,
            "0 = never hold"
        );
    }

    #[test]
    fn the_hooks_own_timeout_caps_the_window() {
        // the old printed setting (timeout 30): the countdown must end before Claude Code kills the hook
        assert_eq!(decide(None, Here::Away, Some(S(30)), 1), S(27));
        assert_eq!(decide(Some(S(300)), Here::Away, Some(S(600)), 1), S(300));
        assert_eq!(decide(None, Here::Away, Some(S(2)), 1), Duration::ZERO);
    }

    #[test]
    fn nobody_to_answer_means_a_short_hold() {
        assert_eq!(decide(None, Here::Away, None, 0), NO_DEVICE);
        assert_eq!(decide(Some(S(5)), Here::Away, None, 0), S(5));
    }

    #[test]
    fn presence_shortens_the_default_and_never_an_explicit_setting() {
        use crate::presence::{FOCUSED_WINDOW, KEYBOARD_WINDOW};
        assert_eq!(decide(None, Here::Focused, None, 1), FOCUSED_WINDOW);
        assert_eq!(decide(None, Here::AtKeyboard, None, 1), KEYBOARD_WINDOW);
        assert_eq!(
            decide(None, Here::Away, None, 1),
            DEFAULT,
            "away: the full window"
        );
        // an explicit setting (or the hook's pin) wins, whatever presence says
        for here in [Here::Focused, Here::AtKeyboard, Here::Away] {
            assert_eq!(decide(Some(S(90)), here, None, 1), S(90));
            assert_eq!(decide(Some(Duration::ZERO), here, None, 1), Duration::ZERO);
        }
        // the other caps still apply to a short window
        assert_eq!(decide(None, Here::AtKeyboard, Some(S(8)), 1), S(5));
        assert_eq!(
            decide(None, Here::AtKeyboard, None, 0),
            KEYBOARD_WINDOW.min(NO_DEVICE)
        );
    }

    #[test]
    fn settings_are_read_as_seconds() {
        assert_eq!(parse_secs(Some("45")), Some(S(45)));
        assert_eq!(parse_secs(Some("# seconds\n\n 120 \n")), Some(S(120)));
        assert_eq!(parse_secs(Some("2.5")), Some(Duration::from_millis(2500)));
        assert_eq!(parse_secs(Some("0")), Some(Duration::ZERO));
        assert_eq!(parse_secs(Some("-1")), None);
        assert_eq!(parse_secs(Some("soon")), None);
        assert_eq!(parse_secs(None), None);
    }

    #[test]
    fn the_hook_timeout_comes_from_agentlink_entries_only() {
        let dir = std::env::temp_dir().join(format!("agentlink-window-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let user = dir.join("user.json");
        let project = dir.join("project.json");
        std::fs::write(
            &user,
            json!({"hooks": {
                "PermissionRequest": [{"hooks": [
                    {"type": "command", "command": "other-tool", "timeout": 1},
                    {"type": "command", "command": "agentlink-hook claude PermissionRequest", "timeout": 30}]}],
                "PreToolUse": [{"matcher": "AskUserQuestion", "hooks": [
                    {"type": "command", "command": "agentlink-hook claude PreToolUse"}]}]}})
            .to_string(),
        )
        .unwrap();
        std::fs::write(&project, "not json").unwrap();
        let files = [user.clone(), project, dir.join("missing.json")];
        assert_eq!(hook_timeout(&files, "PermissionRequest"), Some(S(30)));
        assert_eq!(
            hook_timeout(&files, "PreToolUse"),
            Some(CLAUDE_HOOK_DEFAULT),
            "no timeout: Claude Code's default"
        );
        assert_eq!(hook_timeout(&files, "Stop"), None);
        // a project setting with a shorter timeout wins
        let local = dir.join("local.json");
        std::fs::write(
            &local,
            json!({"hooks": {"PermissionRequest": [{"hooks": [
                {"type": "command", "command": "/opt/bin/agentlink-hook claude PermissionRequest", "timeout": 12.5}]}]}})
            .to_string(),
        )
        .unwrap();
        assert_eq!(
            hook_timeout(&[user, local], "PermissionRequest"),
            Some(Duration::from_millis(12500))
        );
    }
}
