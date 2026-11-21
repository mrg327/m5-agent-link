//! `agentlink hooks print`: config snippets that route agent hooks through `agentlink-hook`.
//! `agentlink hooks statusline`: the `statusLine` setting that feeds the watch's gauges (GAUGES.md).
//! `agentlink hooks list [agent]` / `install <agent>`: check the hooks on disk and add what is missing
//! (DESIGN §6a). Install writes only when something is missing or stale, keeps the file it replaces
//! as `<file>.agentlink-bak`, and merges into Claude Code's settings without touching other hooks.

use anyhow::{bail, Context, Result};
use serde_json::{json, Value};
use std::path::{Path, PathBuf};

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
    "SubagentStop",
];
/// The hook that may block while the watch answers (UI-V2 §7): `PermissionRequest`, a question's
/// (`AskUserQuestion`, LAST-MESSAGE.md §3) included. Its timeout must cover the daemon's longest answer
/// window (300 s, ANSWER-WINDOW.md) with margin: Claude Code kills a hook at its timeout, and the daemon
/// shortens the window to fit a smaller one. The others return at once (`PreToolUse` too since proto 0.18:
/// a question is held at its permission, never twice).
const CLAUDE_BLOCKING: &[&str] = &["PermissionRequest"];
/// Claude Code `timeout` (seconds) for the blocking hooks: the longest window plus 30 s.
const BLOCKING_TIMEOUT_S: u64 = 330;

/// The pi coding agent's extension (`~/.pi/agent/extensions/agentlink.ts`): pi has no shell hooks, so
/// the extension spawns `agentlink-hook pi <Event>` for its events.
const PI_EXTENSION: &str = include_str!("../../../integrations/pi/agentlink.ts");

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
        println!("# pi coding agent: agentlink hooks pi > ~/.pi/agent/extensions/agentlink.ts");
        println!("# Or let agentlink do it: agentlink hooks install copilot|claude|pi (hooks list shows what is there).");
    }
    Ok(())
}

pub fn pi() -> Result<()> {
    print!("{PI_EXTENSION}");
    Ok(())
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, clap::ValueEnum)]
pub enum Agent {
    Copilot,
    Claude,
    Pi,
}

impl Agent {
    const ALL: [Agent; 3] = [Agent::Copilot, Agent::Claude, Agent::Pi];

    fn name(self) -> &'static str {
        match self {
            Agent::Copilot => "copilot",
            Agent::Claude => "claude",
            Agent::Pi => "pi",
        }
    }

    /// The file this agent's hooks live in, under `home`.
    fn path(self, home: &Path) -> PathBuf {
        home.join(match self {
            Agent::Copilot => ".copilot/hooks/agentlink.json",
            Agent::Claude => ".claude/settings.json",
            Agent::Pi => ".pi/agent/extensions/agentlink.ts",
        })
    }
}

#[derive(PartialEq, Eq, Debug)]
enum State {
    Installed,
    Missing,
    /// Some events are routed to the watch, these are not.
    Partial(Vec<String>),
    /// Every event is there, with stale settings (a timeout, an older extension).
    Outdated,
    /// The file is there but is not what it should be (not JSON); install leaves it alone.
    Unreadable(String),
}

impl State {
    fn word(&self) -> &'static str {
        match self {
            State::Installed => "installed",
            State::Missing => "missing",
            State::Partial(_) => "partial",
            State::Outdated => "outdated",
            State::Unreadable(_) => "unreadable",
        }
    }

    fn mark(&self) -> char {
        match self {
            State::Installed => '✓',
            State::Missing => '✗',
            State::Partial(_) | State::Outdated => '~',
            State::Unreadable(_) => '!',
        }
    }
}

fn home() -> Result<PathBuf> {
    std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .map(PathBuf::from)
        .context("no HOME (or USERPROFILE) to find the agents' config in")
}

/// The hooks state of `agent` given its file's contents (`None`: no file).
fn state_of(agent: Agent, text: Option<&str>) -> State {
    let Some(text) = text else {
        return State::Missing;
    };
    if agent == Agent::Pi {
        return if text == PI_EXTENSION {
            State::Installed
        } else {
            State::Outdated
        };
    }
    let v: Value = match serde_json::from_str(text) {
        Ok(v) => v,
        Err(e) => return State::Unreadable(e.to_string()),
    };
    let (events, want) = match agent {
        Agent::Copilot => (COPILOT_EVENTS, copilot()),
        _ => (CLAUDE_EVENTS, claude()),
    };
    let mut missing = Vec::new();
    let mut stale = false;
    for e in events {
        match find_ours(agent, &v["hooks"][e], e) {
            None => missing.push(e.to_string()),
            Some(h) => {
                let w = match agent {
                    Agent::Copilot => &want["hooks"][e][0],
                    _ => &want["hooks"][e][0]["hooks"][0],
                };
                let key = if agent == Agent::Copilot {
                    "timeoutSec"
                } else {
                    "timeout"
                };
                stale |= h[key] != w[key];
            }
        }
    }
    match (missing.len(), stale) {
        (0, false) => State::Installed,
        (0, true) => State::Outdated,
        (n, _) if n == events.len() => State::Missing,
        _ => State::Partial(missing),
    }
}

/// Our hook for `event` in an agent's list of hooks for it. Copilot's `exec` may be an absolute path
/// (`agentlink remote setup` writes one).
fn find_ours<'a>(agent: Agent, list: &'a Value, event: &str) -> Option<&'a Value> {
    let list = list.as_array()?;
    match agent {
        Agent::Copilot => list.iter().find(|h| {
            h["exec"]
                .as_str()
                .is_some_and(|x| x.ends_with("agentlink-hook") || x.ends_with("agentlink-hook.exe"))
                && h["args"] == json!(["copilot", event])
        }),
        _ => {
            let cmd = format!("agentlink-hook claude {event}");
            list.iter()
                .filter_map(|g| g["hooks"].as_array())
                .flatten()
                .find(|h| h["command"].as_str() == Some(cmd.as_str()))
        }
    }
}

/// Claude Code's settings with our hooks put in: any of ours already there are taken out first (and a
/// group left empty by that goes), then one group per event is added after the user's own.
fn merge_claude(mut settings: Value) -> Result<Value> {
    if !settings.is_object() {
        bail!("the settings are not a JSON object");
    }
    if settings["hooks"].is_null() {
        settings["hooks"] = json!({});
    }
    let Some(hooks) = settings["hooks"].as_object_mut() else {
        bail!("\"hooks\" is not a JSON object");
    };
    for (event, ours) in claude()["hooks"].as_object().into_iter().flatten() {
        let list = hooks.entry(event.clone()).or_insert_with(|| json!([]));
        let Some(groups) = list.as_array_mut() else {
            bail!("hooks.{event} is not a list");
        };
        for g in groups.iter_mut() {
            if let Some(hs) = g["hooks"].as_array_mut() {
                hs.retain(|h| {
                    !h["command"]
                        .as_str()
                        .is_some_and(|c| c.starts_with("agentlink-hook claude "))
                });
            }
        }
        groups.retain(|g| !g["hooks"].as_array().is_some_and(|hs| hs.is_empty()));
        groups.extend(ours.as_array().into_iter().flatten().cloned());
    }
    Ok(settings)
}

/// What `install` writes for `agent`, given the file as it is now.
fn installed_text(agent: Agent, current: Option<&str>) -> Result<String> {
    let v = match agent {
        Agent::Pi => return Ok(PI_EXTENSION.to_owned()),
        Agent::Copilot => copilot(),
        Agent::Claude => merge_claude(match current {
            Some(t) => serde_json::from_str(t).context("the settings are not valid JSON")?,
            None => json!({}),
        })?,
    };
    Ok(serde_json::to_string_pretty(&v)? + "\n")
}

fn read(path: &Path) -> Result<Option<String>> {
    match std::fs::read_to_string(path) {
        Ok(t) => Ok(Some(t)),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(e).with_context(|| format!("reading {}", path.display())),
    }
}

/// `path` for the user: under `~` when it is.
fn shown(path: &Path, home: &Path) -> String {
    match path.strip_prefix(home) {
        Ok(rel) => format!("~/{}", rel.display()),
        Err(_) => path.display().to_string(),
    }
}

pub fn list(agent: Option<Agent>, json: bool) -> Result<()> {
    let home = home()?;
    let agents = agent.map_or(Agent::ALL.to_vec(), |a| vec![a]);
    let mut out = Vec::new();
    for a in agents {
        let path = a.path(&home);
        let state = state_of(a, read(&path)?.as_deref());
        if json {
            let mut o = json!({"agent": a.name(), "state": state.word(), "path": path});
            match &state {
                State::Partial(m) => o["missing"] = json!(m),
                State::Unreadable(e) => o["error"] = json!(e),
                _ => {}
            }
            out.push(o);
        } else {
            let detail = match &state {
                State::Partial(m) => format!("  missing {}", m.join(", ")),
                State::Unreadable(e) => format!("  {e}"),
                _ => String::new(),
            };
            println!(
                "{} {:<8} {:<10} {}{detail}",
                state.mark(),
                a.name(),
                state.word(),
                shown(&path, &home)
            );
        }
    }
    if json {
        println!("{}", Value::Array(out));
    }
    Ok(())
}

pub fn install(agent: Agent, json: bool) -> Result<()> {
    let home = home()?;
    let path = agent.path(&home);
    let current = read(&path)?;
    let before = state_of(agent, current.as_deref());
    let changed = match &before {
        State::Installed => false,
        State::Unreadable(e) => bail!(
            "{} is not valid JSON ({e}); fix it or move it away, then run install again",
            path.display()
        ),
        _ => {
            let text = installed_text(agent, current.as_deref())?;
            write_with_backup(&path, current.is_some(), &text)?;
            true
        }
    };
    let backup = (changed && current.is_some()).then(|| backup_of(&path));
    if json {
        println!(
            "{}",
            json!({"agent": agent.name(), "before": before.word(), "changed": changed,
                   "path": path, "backup": backup})
        );
        return Ok(());
    }
    let at = shown(&path, &home);
    if !changed {
        println!("✓ {} already installed  {at}", agent.name());
        return Ok(());
    }
    println!("✓ {} installed  {at}", agent.name());
    if let Some(b) = backup {
        println!("  previous file kept as {}", shown(&b, &home));
    }
    println!("  sessions already running pick the hooks up when restarted");
    if !on_path("agentlink-hook") {
        println!("  warning: agentlink-hook is not on PATH; the hooks call it by name");
    }
    Ok(())
}

fn backup_of(path: &Path) -> PathBuf {
    let mut name = path.file_name().unwrap_or_default().to_os_string();
    name.push(".agentlink-bak");
    path.with_file_name(name)
}

/// Writes `text` to `path` through a temporary file in the same directory, after copying the file it
/// replaces (when `existing`) to its backup.
fn write_with_backup(path: &Path, existing: bool, text: &str) -> Result<()> {
    let dir = path.parent().context("no parent directory")?;
    std::fs::create_dir_all(dir).with_context(|| format!("creating {}", dir.display()))?;
    if existing {
        std::fs::copy(path, backup_of(path))
            .with_context(|| format!("backing up {}", path.display()))?;
    }
    let mut tmp = path.as_os_str().to_os_string();
    tmp.push(".agentlink-tmp");
    std::fs::write(&tmp, text).with_context(|| format!("writing {}", path.display()))?;
    std::fs::rename(&tmp, path).with_context(|| format!("writing {}", path.display()))
}

fn on_path(exe: &str) -> bool {
    let exts: &[&str] = if cfg!(windows) { &[".exe", ""] } else { &[""] };
    std::env::var_os("PATH").is_some_and(|p| {
        std::env::split_paths(&p)
            .any(|d| exts.iter().any(|x| d.join(format!("{exe}{x}")).is_file()))
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_printed_claude_config_has_every_event_the_daemon_uses() {
        let hooks = &claude()["hooks"];
        // PostToolUseFailure carries a failed test run's exit code and StopFailure ends a turn in error
        // (DONE-SUMMARY.md): without them "tests failed" and the summary of an errored turn never arrive.
        // SubagentStop merges a sub-agent's lane of the agent graph (GRAPH.md)
        for e in [
            "PostToolUse",
            "PostToolUseFailure",
            "Stop",
            "StopFailure",
            "SubagentStop",
        ] {
            let h = &hooks[e][0]["hooks"][0];
            assert_eq!(h["command"], format!("agentlink-hook claude {e}"), "{e}");
            assert_eq!(h["timeout"], 5, "{e}: non-blocking");
        }
        assert_eq!(hooks["PermissionRequest"][0]["hooks"][0]["timeout"], 330);
        assert_eq!(
            hooks["PreToolUse"][0]["hooks"][0]["timeout"], 5,
            "a question waits at its permission only"
        );
    }

    fn hooks_text(agent: Agent) -> String {
        installed_text(agent, None).unwrap()
    }

    #[test]
    fn a_fresh_install_is_seen_as_installed_and_a_second_changes_nothing() {
        for a in Agent::ALL {
            let text = hooks_text(a);
            assert_eq!(state_of(a, Some(&text)), State::Installed, "{a:?}");
            assert_eq!(
                installed_text(a, Some(&text)).unwrap(),
                text,
                "{a:?}: idempotent"
            );
        }
        assert_eq!(state_of(Agent::Claude, None), State::Missing);
        assert_eq!(state_of(Agent::Pi, Some("// old")), State::Outdated);
        assert!(matches!(
            state_of(Agent::Claude, Some("{oops")),
            State::Unreadable(_)
        ));
    }

    #[test]
    fn claude_install_keeps_the_users_settings_and_hooks_and_fixes_ours() {
        let user = json!({
            "model": "opus",
            "hooks": {
                "Stop": [{"hooks": [{"type": "command", "command": "say done"}]}],
                "PreToolUse": [{"matcher": "Bash", "hooks": [
                    {"type": "command", "command": "guard.sh"},
                    {"type": "command", "command": "agentlink-hook claude PreToolUse", "timeout": 15}
                ]}]
            },
            "statusLine": {"type": "command", "command": "x"}
        })
        .to_string();
        let st = state_of(Agent::Claude, Some(&user));
        assert!(
            matches!(&st, State::Partial(m) if m.len() == CLAUDE_EVENTS.len() - 1),
            "{st:?}"
        );
        let out: Value =
            serde_json::from_str(&installed_text(Agent::Claude, Some(&user)).unwrap()).unwrap();
        let keys: Vec<_> = out.as_object().unwrap().keys().cloned().collect();
        assert_eq!(keys, ["model", "hooks", "statusLine"], "order kept");
        assert_eq!(out["hooks"]["Stop"][0]["hooks"][0]["command"], "say done");
        assert_eq!(out["hooks"]["Stop"].as_array().unwrap().len(), 2);
        // the stale entry of ours is replaced, the user's hook in that group stays
        let pre = out["hooks"]["PreToolUse"].as_array().unwrap();
        assert_eq!(
            pre[0]["hooks"],
            json!([{"type": "command", "command": "guard.sh"}])
        );
        assert_eq!(pre[1]["hooks"][0]["timeout"], 5);
        assert_eq!(pre.len(), 2);
        assert_eq!(
            state_of(Agent::Claude, Some(&out.to_string())),
            State::Installed
        );
    }

    #[test]
    fn a_stale_timeout_is_outdated_and_a_remote_copilot_exec_counts_as_ours() {
        let mut c = claude();
        c["hooks"]["PermissionRequest"][0]["hooks"][0]["timeout"] = json!(20);
        assert_eq!(
            state_of(Agent::Claude, Some(&c.to_string())),
            State::Outdated
        );
        let remote = copilot_with("/home/me/.agentlink/bin/agentlink-hook").to_string();
        assert_eq!(state_of(Agent::Copilot, Some(&remote)), State::Installed);
    }

    #[test]
    fn install_writes_a_backup_of_the_file_it_replaces() {
        let dir = std::env::temp_dir().join(format!("agentlink-hooks-{}", std::process::id()));
        let path = Agent::Claude.path(&dir);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(&path, r#"{"model":"opus"}"#).unwrap();
        let text = installed_text(Agent::Claude, Some(r#"{"model":"opus"}"#)).unwrap();
        write_with_backup(&path, true, &text).unwrap();
        assert_eq!(
            std::fs::read_to_string(backup_of(&path)).unwrap(),
            r#"{"model":"opus"}"#
        );
        assert_eq!(
            state_of(Agent::Claude, read(&path).unwrap().as_deref()),
            State::Installed
        );
        std::fs::remove_dir_all(&dir).unwrap();
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
