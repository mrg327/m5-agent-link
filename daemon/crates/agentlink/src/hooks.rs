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
    "Notification",
    "Stop",
];

/// `~/.copilot/hooks/agentlink.json`
fn copilot() -> Value {
    let hooks: serde_json::Map<String, Value> = COPILOT_EVENTS
        .iter()
        .map(|e| {
            let h = json!([{"type": "command", "exec": "agentlink-hook", "args": ["copilot", e], "timeoutSec": 5}]);
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
            let h = json!([{"hooks": [{"type": "command", "command": format!("agentlink-hook claude {e}"), "timeout": 5}]}]);
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
