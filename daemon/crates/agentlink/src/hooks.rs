//! `agentlink hooks print`: config snippets that route agent hooks through `agentlink-hook`.
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
    "PermissionRequest",
    "Notification",
    "Stop",
];
/// Hooks that may block while the watch answers (UI-V2 §7): `PermissionRequest`, and
/// `PreToolUse` for `AskUserQuestion`. Their timeout must cover `AGENTLINK_WAIT_MS` (15 s by
/// default) with margin; the others return at once.
const CLAUDE_BLOCKING: &[&str] = &["PreToolUse", "PermissionRequest"];

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
            let timeout = if CLAUDE_BLOCKING.contains(e) { 30 } else { 5 };
            let h = json!([{"hooks": [{"type": "command", "command": format!("agentlink-hook claude {e}"), "timeout": timeout}]}]);
            (e.to_string(), h)
        })
        .collect();
    json!({"hooks": hooks})
}

pub fn print(json: bool) -> Result<()> {
    if json {
        println!("{}", json!({"copilot": copilot(), "claude": claude()}));
    } else {
        println!("# GitHub Copilot CLI: save as ~/.copilot/hooks/agentlink.json");
        println!("{}\n", serde_json::to_string_pretty(&copilot())?);
        println!("# Claude Code: merge into ~/.claude/settings.json");
        println!("{}", serde_json::to_string_pretty(&claude())?);
    }
    Ok(())
}
