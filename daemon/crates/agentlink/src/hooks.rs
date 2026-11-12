//! `agentlink hooks print`: config snippets that route agent hooks through `agentlink-hook`.
//! `agentlink hooks statusline`: the `statusLine` setting that feeds the watch's gauges (GAUGES.md).
//! Nothing is written to disk; installation comes later (DESIGN §6a).

use anyhow::Result;
use serde_json::{json, Value};

const COPILOT_EVENTS: &[&str] = &[
    "sessionStart",
    "sessionEnd",
    "userPromptSubmitted",
    "preToolUse",
    "postToolUse",
    "agentStop",
    "errorOccurred",
    "notification",
];
const CLAUDE_EVENTS: &[&str] = &[
    "SessionStart",
    "SessionEnd",
    "UserPromptSubmit",
    "PreToolUse",
    "PostToolUse",
    "PostToolUseFailure",
    "PermissionRequest",
    "Notification",
    "Stop",
    "StopFailure",
];
/// Hooks that may block while the watch answers (UI-V2 §7): `PermissionRequest`, and
/// `PreToolUse` for `AskUserQuestion`. Their timeout must cover the daemon's longest answer window
/// (300 s, ANSWER-WINDOW.md) with margin: Claude Code kills a hook at its timeout, and the daemon
/// shortens the window to fit a smaller one. The others return at once.
const CLAUDE_BLOCKING: &[&str] = &["PreToolUse", "PermissionRequest"];
/// Claude Code `timeout` (seconds) for the blocking hooks: the longest window plus 30 s.
const BLOCKING_TIMEOUT_S: u64 = 330;

/// `~/.copilot/hooks/agentlink.json`
fn copilot() -> Value {
    copilot_with("agentlink-hook")
}

/// The Copilot hooks config with `exec` set to `exe` (an absolute path on remotes).
pub(crate) fn copilot_with(exe: &str) -> Value {
    let hooks: serde_json::Map<String, Value> = COPILOT_EVENTS
        .iter()
        .map(|e| {
            let h =
                json!([{"type": "command", "exec": exe, "args": ["copilot", e], "timeoutSec": 5}]);
            (e.to_string(), h)
        })
        .collect();
    json!({"version": 1, "hooks": hooks})
}

/// The `hooks` block of `~/.claude/settings.json`
fn claude() -> Value {
    let hooks: serde_json::Map<String, Value> = CLAUDE_EVENTS
        .iter()
        .map(|e| {
            let timeout = if CLAUDE_BLOCKING.contains(e) { BLOCKING_TIMEOUT_S } else { 5 };
            let h = json!([{"hooks": [{"type": "command", "command": format!("agentlink-hook claude {e}"), "timeout": timeout}]}]);
            (e.to_string(), h)
        })
        .collect();
    json!({"hooks": hooks})
}

/// `cmd` as one POSIX shell word.
fn sh_quote(cmd: &str) -> String {
    format!("'{}'", cmd.replace('\'', r"'\''"))
}

/// The `statusLine` block of `~/.claude/settings.json`: the watch's entry point, which runs `chain` (the
/// user's own status-line command) after it with the same input and prints what that prints.
fn statusline_block(chain: Option<&str>) -> Value {
    let chain = chain.map(str::trim).filter(|c| !c.is_empty());
    let command = match chain {
        Some(c) => format!("agentlink-hook statusline -- {}", sh_quote(c)),
        None => "agentlink-hook statusline".to_owned(),
    };
    json!({"statusLine": {"type": "command", "command": command}})
}

/// The status-line command a Claude Code settings file already has (read only), unless it is ours.
fn existing_statusline(settings: &str) -> Option<String> {
    let v: Value = serde_json::from_str(settings).ok()?;
    let cmd = v["statusLine"]["command"].as_str()?.trim();
    (!cmd.is_empty() && !cmd.starts_with("agentlink-hook statusline")).then(|| cmd.to_owned())
}

pub fn statusline(chain: Option<String>, json: bool) -> Result<()> {
    let settings = std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .map(|h| std::path::PathBuf::from(h).join(".claude/settings.json"));
    let found = chain.is_none().then(|| {
        settings
            .as_ref()
            .and_then(|p| std::fs::read_to_string(p).ok())
            .and_then(|s| existing_statusline(&s))
    });
    let chain = chain.or(found.clone().flatten());
    let block = statusline_block(chain.as_deref());
    if json {
        println!("{block}");
        return Ok(());
    }
    println!("# Claude Code: merge into ~/.claude/settings.json (replaces the statusLine entry).");
    match (&chain, found.flatten()) {
        (Some(_), Some(_)) => println!(
            "# Your current status-line command is kept: it runs after the watch's, with the same input."
        ),
        (Some(_), None) => println!("# Your command runs after the watch's, with the same input."),
        (None, _) => println!(
            "# No status line of your own: it stays empty. Add one with --chain '<command>'."
        ),
    }
    println!("{}", serde_json::to_string_pretty(&block)?);
    Ok(())
}

pub fn print(json: bool) -> Result<()> {
    if json {
        println!("{}", json!({"copilot": copilot(), "claude": claude()}));
    } else {
        println!("# GitHub Copilot CLI: save as ~/.copilot/hooks/agentlink.json");
        println!("{}\n", serde_json::to_string_pretty(&copilot())?);
        println!("# Claude Code: merge into ~/.claude/settings.json");
        println!("{}", serde_json::to_string_pretty(&claude())?);
        println!("\n# Usage and context gauges on the watch: see `agentlink hooks statusline`.");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_printed_claude_config_has_every_event_the_daemon_uses() {
        let hooks = &claude()["hooks"];
        // PostToolUseFailure carries a failed test run's exit code and StopFailure ends a turn in error
        // (DONE-SUMMARY.md): without them "tests failed" and the summary of an errored turn never arrive
        for e in ["PostToolUse", "PostToolUseFailure", "Stop", "StopFailure"] {
            let h = &hooks[e][0]["hooks"][0];
            assert_eq!(h["command"], format!("agentlink-hook claude {e}"), "{e}");
            assert_eq!(h["timeout"], 5, "{e}: non-blocking");
        }
        assert_eq!(hooks["PermissionRequest"][0]["hooks"][0]["timeout"], 330);
    }

    #[test]
    fn statusline_block_chains_the_users_command_as_one_word() {
        assert_eq!(
            statusline_block(None)["statusLine"]["command"],
            "agentlink-hook statusline"
        );
        let jq = r#"jq -r '"[\(.model.display_name)]"'"#;
        let cmd = statusline_block(Some(jq))["statusLine"]["command"]
            .as_str()
            .unwrap()
            .to_owned();
        assert_eq!(
            cmd,
            r#"agentlink-hook statusline -- 'jq -r '\''"[\(.model.display_name)]"'\'''"#
        );
        // the shell gives the shim back exactly the user's command
        let out = std::process::Command::new("sh")
            .args(["-c", &format!("printf %s {}", sh_quote(jq))])
            .output()
            .unwrap();
        assert_eq!(String::from_utf8_lossy(&out.stdout), jq);
    }

    #[test]
    fn an_existing_status_line_is_found_and_ours_is_not_chained_twice() {
        let s = r#"{"statusLine": {"type": "command", "command": "~/.claude/statusline.sh"}}"#;
        assert_eq!(
            existing_statusline(s).as_deref(),
            Some("~/.claude/statusline.sh")
        );
        let ours =
            r#"{"statusLine": {"type": "command", "command": "agentlink-hook statusline -- x"}}"#;
        assert_eq!(existing_statusline(ours), None);
        assert_eq!(existing_statusline("{}"), None);
        assert_eq!(existing_statusline("not json"), None);
    }
}
