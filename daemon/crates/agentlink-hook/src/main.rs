//! agentlink-hook: called by agent hooks (`agentlink-hook <agent> <event>`).
//!
//! Reads the hook JSON from stdin, adds agent kind and terminal-identifying env, and sends
//! one line to the daemon socket. It must never hold up the agent: everything runs under a
//! hard deadline, every failure is swallowed, the exit code is always 0, and stdout stays
//! empty except for a permission hook's reply (none yet: the daemon sends no decisions).

use serde_json::{json, Map, Value};
use std::io::Read;
use std::sync::mpsc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const DEADLINE: Duration = Duration::from_millis(1500);
const STDIN_TIMEOUT: Duration = Duration::from_millis(500);
const REPLY_TIMEOUT: Duration = Duration::from_millis(300);

/// Env that identifies the terminal/window the agent runs in (used by focus drivers).
const ENV_KEYS: &[&str] = &[
    "TERM_PROGRAM",
    "TERM_PROGRAM_VERSION",
    "TERM",
    "KITTY_WINDOW_ID",
    "KITTY_LISTEN_ON",
    "WT_SESSION",
    "WT_PROFILE_ID",
    "ITERM_SESSION_ID",
    "TERM_SESSION_ID",
    "TMUX",
    "TMUX_PANE",
    "WEZTERM_PANE",
    "WINDOWID",
    "VSCODE_PID",
    "SSH_TTY",
    "COPILOT_HOME",
];

fn socket_path() -> std::path::PathBuf {
    if let Some(h) = std::env::var_os("AGENTLINK_HOME") {
        return std::path::PathBuf::from(h).join("hook.sock");
    }
    let home = std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .unwrap_or_default();
    std::path::PathBuf::from(home)
        .join(".agentlink")
        .join("hook.sock")
}

fn is_permission_hook(event: &str) -> bool {
    event.eq_ignore_ascii_case("permissionrequest")
}

fn read_stdin() -> String {
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let mut s = String::new();
        let _ = std::io::stdin().take(1 << 20).read_to_string(&mut s);
        let _ = tx.send(s);
    });
    rx.recv_timeout(STDIN_TIMEOUT).unwrap_or_default()
}

fn build_line(agent: &str, event: &str, stdin: &str) -> String {
    let payload: Value = serde_json::from_str(stdin).unwrap_or(Value::Null);
    let env: Map<String, Value> = ENV_KEYS
        .iter()
        .filter_map(|k| Some((k.to_string(), Value::String(std::env::var(k).ok()?))))
        .collect();
    let cwd = std::env::current_dir()
        .ok()
        .map(|p| p.to_string_lossy().into_owned());
    #[cfg(unix)]
    let ppid = std::os::unix::process::parent_id();
    #[cfg(not(unix))]
    let ppid = 0u32;
    let ts = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let event = if event.is_empty() {
        payload["hook_event_name"].as_str().unwrap_or("")
    } else {
        event
    };
    json!({"agent": agent, "event": event, "ts": ts, "cwd": cwd, "ppid": ppid, "env": env, "payload": payload})
        .to_string()
}

/// Sends `line`; for permission hooks also waits briefly for a one-line reply.
#[cfg(unix)]
fn send(line: &str, want_reply: bool) -> Option<String> {
    use std::io::{BufRead, BufReader, Write};
    use std::net::Shutdown;
    use std::os::unix::net::UnixStream;

    let mut s = UnixStream::connect(socket_path()).ok()?;
    s.set_write_timeout(Some(Duration::from_millis(300))).ok()?;
    s.write_all(line.as_bytes()).ok()?;
    s.write_all(b"\n").ok()?;
    s.shutdown(Shutdown::Write).ok()?;
    if !want_reply {
        return None;
    }
    s.set_read_timeout(Some(REPLY_TIMEOUT)).ok()?;
    let mut reply = String::new();
    BufReader::new(s).read_line(&mut reply).ok()?;
    Some(reply.trim().to_owned()).filter(|r| !r.is_empty())
}

// TODO(windows): connect to the daemon's named pipe.
#[cfg(not(unix))]
fn send(_line: &str, _want_reply: bool) -> Option<String> {
    None
}

fn main() {
    let mut args = std::env::args().skip(1);
    let agent = args.next().unwrap_or_else(|| "other".into());
    let event = args.next().unwrap_or_default();

    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let line = build_line(&agent, &event, &read_stdin());
        let ev = serde_json::from_str::<Value>(&line)
            .ok()
            .and_then(|v| v["event"].as_str().map(str::to_owned));
        let want_reply = ev.as_deref().is_some_and(is_permission_hook);
        let _ = tx.send((want_reply, send(&line, want_reply)));
    });
    if let Ok((true, Some(reply))) = rx.recv_timeout(DEADLINE) {
        println!("{reply}");
    }
    std::process::exit(0);
}
