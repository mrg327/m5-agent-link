//! agentlink-hook: called by agent hooks (`agentlink-hook <agent> <event>`).
//!
//! Reads the hook JSON from stdin, adds agent kind and terminal-identifying env, and sends
//! one line to the daemon socket. It must never hold up the agent: everything runs under a
//! hard deadline, every failure is swallowed, the exit code is always 0, and stdout stays
//! empty unless the daemon returns a decision.
//!
//! Two Claude Code hooks block for the watch (UI-V2 §7, DESIGN §6a "first answer wins"):
//! `PermissionRequest` (fires when Claude Code is about to show its permission prompt) and
//! `PreToolUse` for `AskUserQuestion`. The hook holds the prompt back for `AGENTLINK_WAIT_MS`
//! (15 s) while the daemon shows the request on the watch. A device answer becomes the
//! event's own hook output; no answer (timeout, terminal, daemon down) prints nothing and
//! exits 0, which Claude Code documents as "no decision": its normal prompt appears. Silence
//! never approves. Format: https://code.claude.com/docs/en/hooks (PermissionRequest
//! `hookSpecificOutput.decision.behavior`; PreToolUse `permissionDecision` + `updatedInput`).

use serde_json::{json, Map, Value};
use std::io::{BufRead, Read};
use std::sync::mpsc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const DEADLINE: Duration = Duration::from_millis(1500);
const STDIN_TIMEOUT: Duration = Duration::from_millis(500);
/// How long a blocking hook waits for the watch before letting the terminal prompt appear.
const DEFAULT_WAIT_MS: u64 = 15_000;
const MAX_WAIT_MS: u64 = 120_000;
/// Slack between the daemon's own timeout and ours, so the daemon's `none` arrives first.
const WAIT_SLACK: Duration = Duration::from_millis(2000);

/// Env that identifies the terminal/window the agent runs in (used by focus drivers).
const ENV_KEYS: &[&str] = &[
    "TERM_PROGRAM",
    "TERM_PROGRAM_VERSION",
    "TERM",
    "KITTY_WINDOW_ID",
    "KITTY_LISTEN_ON",
    "KITTY_PID",
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

fn agentlink_home() -> std::path::PathBuf {
    if let Some(h) = std::env::var_os("AGENTLINK_HOME") {
        return std::path::PathBuf::from(h);
    }
    let home = std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .unwrap_or_default();
    std::path::PathBuf::from(home).join(".agentlink")
}

fn socket_path() -> std::path::PathBuf {
    agentlink_home().join("hook.sock")
}

/// Branch of the repo containing `cwd` (same rule as the daemon's `branch_of`).
fn branch_of(cwd: &str) -> Option<String> {
    let head = std::path::Path::new(cwd)
        .ancestors()
        .find_map(|d| std::fs::read_to_string(d.join(".git/HEAD")).ok())?;
    head.trim()
        .strip_prefix("ref: refs/heads/")
        .map(str::to_owned)
}

/// Which Claude Code hooks wait for a decision: the permission prompt, and a question.
fn blocks_for_watch(agent: &str, event: &str, payload: &Value) -> bool {
    agent == "claude"
        && (event.eq_ignore_ascii_case("permissionrequest")
            || (event.eq_ignore_ascii_case("pretooluse")
                && payload["tool_name"].as_str() == Some("AskUserQuestion")))
}

fn wait_ms() -> u64 {
    std::env::var("AGENTLINK_WAIT_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(DEFAULT_WAIT_MS)
        .min(MAX_WAIT_MS)
}

/// Turns the daemon's `{"decision": "allow"|"deny"|"none", "grant"?, "answer"?}` into the
/// hook output Claude Code expects for `event`. `None` = print nothing (no decision).
fn hook_output(event: &str, payload: &Value, reply: &Value) -> Option<Value> {
    let decision = reply["decision"].as_str()?;
    if decision == "none" {
        return None;
    }
    if event.eq_ignore_ascii_case("permissionrequest") {
        let mut d = json!({"behavior": decision});
        if decision == "allow" {
            // "Yes, don't ask again": apply the rules Claude Code itself proposed for this call
            if reply["grant"].as_bool() == Some(true) {
                if let Some(s) = payload["permission_suggestions"]
                    .as_array()
                    .filter(|a| !a.is_empty())
                {
                    d["updatedPermissions"] = Value::Array(s.clone());
                }
            }
        } else if decision == "deny" {
            d["message"] = json!("Denied from the watch");
        } else {
            return None;
        }
        return Some(
            json!({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": d}}),
        );
    }
    if event.eq_ignore_ascii_case("pretooluse") {
        // AskUserQuestion: "allow" alone is not an answer; the chosen label goes in updatedInput.answers
        let answer = reply["answer"].as_str()?;
        let mut input = payload["tool_input"].clone();
        let question = input["questions"][0]["question"].as_str()?.to_owned();
        input["answers"] = json!({ question: answer });
        return Some(json!({"hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "allow",
            "permissionDecisionReason": "answered on the watch",
            "updatedInput": input,
        }}));
    }
    None
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

/// Walks up from `start` through `ps` output rows (`pid ppid tty`), returning up to `max`
/// pids (nearest first) and the first real controlling tty found on the way.
#[cfg_attr(not(unix), allow(dead_code))]
fn walk_chain(ps: &str, start: u32, max: usize) -> (Vec<u32>, Option<String>) {
    let rows: std::collections::HashMap<u32, (u32, &str)> = ps
        .lines()
        .filter_map(|l| {
            let mut f = l.split_whitespace();
            let pid = f.next()?.parse().ok()?;
            let ppid = f.next()?.parse().ok()?;
            Some((pid, (ppid, f.next().unwrap_or(""))))
        })
        .collect();
    let (mut chain, mut tty) = (Vec::new(), None);
    let mut pid = start;
    while pid > 1 && chain.len() < max && !chain.contains(&pid) {
        chain.push(pid);
        let Some(&(ppid, t)) = rows.get(&pid) else {
            break;
        };
        if tty.is_none() && !t.is_empty() && !t.starts_with('?') {
            tty = Some(if t.starts_with('/') {
                t.to_owned()
            } else {
                format!("/dev/{t}")
            });
        }
        pid = ppid;
    }
    (chain, tty)
}

/// Parent pid chain and controlling tty of the agent. Our own stdio is piped, so the tty
/// is found on the ancestors. One short `ps` call, bounded by a timeout.
#[cfg(unix)]
fn process_identity(ppid: u32) -> (Vec<u32>, Option<String>) {
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let out = std::process::Command::new("ps")
            .args(["-A", "-o", "pid=,ppid=,tty="])
            .stdin(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .output();
        let _ = tx.send(out.ok());
    });
    match rx.recv_timeout(Duration::from_millis(500)) {
        Ok(Some(o)) => walk_chain(&String::from_utf8_lossy(&o.stdout), ppid, 8),
        _ => (vec![ppid], None),
    }
}

/// Parent pid chain via a Toolhelp snapshot, formatted like `ps` rows (`pid ppid`) so
/// `walk_chain` can be shared. Windows consoles have no tty.
#[cfg(windows)]
fn process_table() -> String {
    use std::fmt::Write;
    use windows::Win32::Foundation::CloseHandle;
    use windows::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
        TH32CS_SNAPPROCESS,
    };

    let mut out = String::new();
    // SAFETY: plain Win32 calls; the entry is initialised with its size and the handle is
    // closed before returning.
    unsafe {
        let Ok(snap) = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) else {
            return out;
        };
        let mut e = PROCESSENTRY32W {
            dwSize: std::mem::size_of::<PROCESSENTRY32W>() as u32,
            ..Default::default()
        };
        let mut ok = Process32FirstW(snap, &mut e).is_ok();
        while ok {
            let _ = writeln!(out, "{} {}", e.th32ProcessID, e.th32ParentProcessID);
            ok = Process32NextW(snap, &mut e).is_ok();
        }
        let _ = CloseHandle(snap);
    }
    out
}

/// Parent of `pid` in a `pid ppid` table.
#[cfg_attr(not(windows), allow(dead_code))]
fn parent_of(table: &str, pid: u32) -> Option<u32> {
    table.lines().find_map(|l| {
        let mut f = l.split_whitespace();
        (f.next()?.parse::<u32>().ok()? == pid).then(|| f.next()?.parse().ok())?
    })
}

#[cfg(windows)]
fn process_identity(table: &str, ppid: u32) -> (Vec<u32>, Option<String>) {
    let (chain, _) = walk_chain(table, ppid, 8);
    if chain.is_empty() {
        (vec![ppid], None)
    } else {
        (chain, None)
    }
}

#[cfg(not(any(unix, windows)))]
fn process_identity(_ppid: u32) -> (Vec<u32>, Option<String>) {
    (Vec::new(), None)
}

/// The line for the daemon, plus the hook's event name and payload (for formatting a reply)
/// and how long it waits for one (0: no wait).
fn build_line(agent: &str, event: &str, stdin: &str, remote: bool) -> (String, String, Value, u64) {
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
    #[cfg(windows)]
    let table = process_table();
    #[cfg(windows)]
    let ppid = parent_of(&table, std::process::id()).unwrap_or(0);
    #[cfg(not(any(unix, windows)))]
    let ppid = 0u32;
    #[cfg(windows)]
    let (pids, tty) = process_identity(&table, ppid);
    #[cfg(not(windows))]
    let (pids, tty) = process_identity(ppid);
    let ts = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let event = if event.is_empty() {
        payload["hook_event_name"].as_str().unwrap_or("")
    } else {
        event
    };
    let wait = if blocks_for_watch(agent, event, &payload) {
        wait_ms()
    } else {
        0
    };
    let mut line = json!({"agent": agent, "event": event, "ts": ts, "cwd": cwd, "ppid": ppid, "tty": tty, "pids": pids, "env": env, "payload": payload});
    if remote {
        // the daemon cannot read a remote filesystem, so the hook reports the branch
        let dir = payload["cwd"].as_str().or(cwd.as_deref()).unwrap_or("");
        if let Some(b) = branch_of(dir).filter(|_| !dir.is_empty()) {
            line["branch"] = json!(b);
        }
    }
    if wait > 0 {
        line["await"] = json!({"wait_ms": wait});
    }
    (line.to_string(), event.to_owned(), payload, wait)
}

/// Writes each line newline-terminated.
fn write_lines(w: &mut impl std::io::Write, lines: &[String]) -> Option<()> {
    for l in lines {
        w.write_all(l.as_bytes()).ok()?;
        w.write_all(b"\n").ok()?;
    }
    w.flush().ok()
}

/// Reads one reply line; `None` when empty or on error.
fn read_reply(r: impl std::io::Read) -> Option<String> {
    let mut reply = String::new();
    std::io::BufReader::new(r).read_line(&mut reply).ok()?;
    Some(reply.trim().to_owned()).filter(|r| !r.is_empty())
}

/// Sends `lines` to the local daemon; for blocking hooks also waits up to `reply_wait` for
/// a one-line reply.
#[cfg(unix)]
fn send_local(lines: &[String], reply_wait: Option<Duration>) -> Option<String> {
    use std::net::Shutdown;
    use std::os::unix::net::UnixStream;

    let mut s = UnixStream::connect(socket_path()).ok()?;
    s.set_write_timeout(Some(Duration::from_millis(300))).ok()?;
    write_lines(&mut s, lines)?;
    s.shutdown(Shutdown::Write).ok()?;
    s.set_read_timeout(Some(reply_wait?)).ok()?;
    read_reply(s)
}

/// `AGENTLINK_REMOTE=tcp:<loopback>:<port>` -> (host to resolve, port). Only loopback is
/// accepted, so prompt text can never leave the machine unencrypted.
fn parse_remote(spec: &str) -> Option<(&'static str, u16)> {
    let (addr, port) = spec.strip_prefix("tcp:")?.rsplit_once(':')?;
    let host = match addr {
        "127.0.0.1" => "127.0.0.1",
        "localhost" => "localhost",
        "[::1]" => "::1",
        _ => return None,
    };
    Some((host, port.parse().ok().filter(|p| *p != 0)?))
}

fn remote_token() -> Option<String> {
    let t = std::fs::read_to_string(agentlink_home().join("remote.token")).ok()?;
    Some(t.trim().to_owned()).filter(|t| !t.is_empty())
}

fn host_name() -> String {
    ["COMPUTERNAME", "HOSTNAME"]
        .iter()
        .find_map(|k| std::env::var(k).ok().filter(|v| !v.is_empty()))
        // bash sets HOSTNAME without exporting it.
        .or_else(|| {
            ["/proc/sys/kernel/hostname", "/etc/hostname"]
                .iter()
                .find_map(|f| std::fs::read_to_string(f).ok())
                .map(|h| h.trim().to_owned())
                .filter(|h| !h.is_empty())
        })
        .unwrap_or_else(|| "remote".into())
}

fn auth_line(token: &str, host: &str, os: &str) -> String {
    json!({"auth": token, "host": host, "os": os}).to_string()
}

fn local_os() -> &'static str {
    if cfg!(windows) {
        "win"
    } else if cfg!(target_os = "macos") {
        "mac"
    } else {
        "linux"
    }
}

/// Sends the auth line and then `lines` over the (ssh-forwarded) loopback TCP port.
fn send_tcp(
    addr: (&str, u16),
    auth: String,
    lines: &[String],
    reply_wait: Option<Duration>,
) -> Option<String> {
    use std::net::{Shutdown, TcpStream, ToSocketAddrs};

    let connect = Duration::from_millis(300);
    let mut s = addr
        .to_socket_addrs()
        .ok()?
        .find_map(|a| TcpStream::connect_timeout(&a, connect).ok())?;
    s.set_write_timeout(Some(connect)).ok()?;
    let all: Vec<String> = std::iter::once(auth).chain(lines.iter().cloned()).collect();
    write_lines(&mut s, &all)?;
    s.shutdown(Shutdown::Write).ok()?;
    s.set_read_timeout(Some(reply_wait?)).ok()?;
    read_reply(s)
}

/// Remote transport from `AGENTLINK_REMOTE`; a bad spec or missing token sends nothing.
fn send_remote(spec: &str, lines: &[String], reply_wait: Option<Duration>) -> Option<String> {
    let Some(addr) = parse_remote(spec) else {
        eprintln!("agentlink-hook: AGENTLINK_REMOTE must be tcp:127.0.0.1:<port>; not sending");
        return None;
    };
    let auth = auth_line(&remote_token()?, &host_name(), local_os());
    send_tcp(addr, auth, lines, reply_wait)
}

fn send(lines: &[String], reply_wait: Option<Duration>) -> Option<String> {
    match std::env::var("AGENTLINK_REMOTE") {
        Ok(spec) => send_remote(&spec, lines, reply_wait),
        Err(std::env::VarError::NotUnicode(_)) => None,
        Err(_) => send_local(lines, reply_wait),
    }
}

/// Per-user pipe name; must match `paths::hook_pipe_name` in agentlinkd. `AGENTLINK_PIPE`
/// overrides it, otherwise it is derived from the socket path (so `AGENTLINK_HOME` works).
#[cfg_attr(not(windows), allow(dead_code))]
fn pipe_name(socket: &std::path::Path) -> String {
    if let Some(n) = std::env::var_os("AGENTLINK_PIPE") {
        return n.to_string_lossy().into_owned();
    }
    let user = std::env::var("USERNAME").unwrap_or_default();
    let user: String = user
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect();
    let key = socket.to_string_lossy().to_lowercase();
    let hash = key.bytes().fold(0xcbf2_9ce4_8422_2325u64, |h, b| {
        (h ^ u64::from(b)).wrapping_mul(0x100_0000_01b3)
    });
    format!(r"\\.\pipe\agentlink-{user}-{hash:016x}")
}

/// Windows twin of the unix `send_local`: the lines over the named pipe, then an optional
/// reply read on a helper thread so a silent daemon cannot hold us past the wait.
#[cfg(windows)]
fn send_local(lines: &[String], reply_wait: Option<Duration>) -> Option<String> {
    const ERROR_PIPE_BUSY: i32 = 231;
    let name = pipe_name(&socket_path());
    let mut tries = 0;
    let mut pipe = loop {
        match std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(&name)
        {
            Ok(f) => break f,
            Err(e) if e.raw_os_error() == Some(ERROR_PIPE_BUSY) && tries < 5 => {
                tries += 1;
                std::thread::sleep(Duration::from_millis(20));
            }
            Err(_) => return None,
        }
    };
    write_lines(&mut pipe, lines)?;
    let wait = reply_wait?;
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let _ = tx.send(read_reply(pipe));
    });
    rx.recv_timeout(wait).ok()?
}

#[cfg(not(any(unix, windows)))]
fn send_local(_lines: &[String], _reply_wait: Option<Duration>) -> Option<String> {
    None
}

fn main() {
    let mut args = std::env::args().skip(1);
    let agent = args.next().unwrap_or_else(|| "other".into());
    let event = args.next().unwrap_or_default();

    // The worker reads stdin and talks to the socket, so neither can hold the agent past the
    // deadline. It first reports what kind of hook this is, then the daemon's reply.
    let (kind_tx, kind_rx) = mpsc::channel();
    let (reply_tx, reply_rx) = mpsc::channel();
    std::thread::spawn(move || {
        let remote = std::env::var_os("AGENTLINK_REMOTE").is_some();
        let (line, event, payload, wait) = build_line(&agent, &event, &read_stdin(), remote);
        let _ = kind_tx.send((event, payload, wait));
        let reply_wait = (wait > 0).then(|| Duration::from_millis(wait) + WAIT_SLACK);
        let _ = reply_tx.send(send(&[line], reply_wait));
    });
    let Ok((event, payload, wait)) = kind_rx.recv_timeout(DEADLINE) else {
        std::process::exit(0);
    };
    let budget = if wait > 0 {
        Duration::from_millis(wait) + 2 * WAIT_SLACK
    } else {
        DEADLINE
    };
    if let Ok(Some(reply)) = reply_rx.recv_timeout(budget) {
        if let Some(out) = serde_json::from_str::<Value>(&reply)
            .ok()
            .and_then(|r| hook_output(&event, &payload, &r))
        {
            println!("{out}");
        }
    }
    std::process::exit(0);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chain_finds_first_tty_and_stops_at_init() {
        let ps = "  500   1 ??\n  600 500 ttys003\n  700 600 ttys003\n  800 700 ??\n";
        let (chain, tty) = walk_chain(ps, 800, 8);
        assert_eq!(chain, vec![800, 700, 600, 500]);
        assert_eq!(tty.as_deref(), Some("/dev/ttys003"));
        assert_eq!(walk_chain(ps, 800, 2).0, vec![800, 700]);
        assert_eq!(walk_chain("", 42, 8), (vec![42], None));
    }

    #[test]
    fn permission_outputs_follow_claude_codes_format() {
        let payload = json!({"tool_name": "Bash", "tool_input": {"command": "npm test"},
                             "permission_suggestions": [{"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "npm test:*"}]}]});
        let allow =
            hook_output("PermissionRequest", &payload, &json!({"decision": "allow"})).unwrap();
        assert_eq!(
            allow["hookSpecificOutput"]["hookEventName"],
            "PermissionRequest"
        );
        assert_eq!(allow["hookSpecificOutput"]["decision"]["behavior"], "allow");
        assert!(allow["hookSpecificOutput"]["decision"]
            .get("updatedPermissions")
            .is_none());

        let grant = hook_output(
            "PermissionRequest",
            &payload,
            &json!({"decision": "allow", "grant": true}),
        )
        .unwrap();
        assert_eq!(
            grant["hookSpecificOutput"]["decision"]["updatedPermissions"],
            payload["permission_suggestions"]
        );

        let deny =
            hook_output("PermissionRequest", &payload, &json!({"decision": "deny"})).unwrap();
        assert_eq!(deny["hookSpecificOutput"]["decision"]["behavior"], "deny");
        assert!(deny["hookSpecificOutput"]["decision"]["message"].is_string());

        // no decision, garbage, or an unknown decision word: print nothing, so the terminal prompts
        assert!(hook_output("PermissionRequest", &payload, &json!({"decision": "none"})).is_none());
        assert!(hook_output("PermissionRequest", &payload, &json!({})).is_none());
        assert!(
            hook_output("PermissionRequest", &payload, &json!({"decision": "maybe"})).is_none()
        );
    }

    #[test]
    fn question_answer_goes_into_updated_input() {
        let payload = json!({"tool_name": "AskUserQuestion", "tool_input": {"questions": [
            {"question": "Which framework?", "header": "Framework", "options": [{"label": "React"}, {"label": "Vue"}]}]}});
        let out = hook_output(
            "PreToolUse",
            &payload,
            &json!({"decision": "allow", "answer": "Vue"}),
        )
        .unwrap();
        let h = &out["hookSpecificOutput"];
        assert_eq!(h["hookEventName"], "PreToolUse");
        assert_eq!(h["permissionDecision"], "allow");
        assert_eq!(h["updatedInput"]["answers"]["Which framework?"], "Vue");
        assert_eq!(
            h["updatedInput"]["questions"],
            payload["tool_input"]["questions"]
        );
        // "allow" without an answer is not an answer to a question
        assert!(hook_output("PreToolUse", &payload, &json!({"decision": "allow"})).is_none());
        assert!(hook_output("PreToolUse", &payload, &json!({"decision": "none"})).is_none());
    }

    #[test]
    fn only_claude_permission_and_question_hooks_block() {
        let ask = json!({"tool_name": "AskUserQuestion"});
        let bash = json!({"tool_name": "Bash"});
        assert!(blocks_for_watch("claude", "PermissionRequest", &bash));
        assert!(blocks_for_watch("claude", "PreToolUse", &ask));
        assert!(!blocks_for_watch("claude", "PreToolUse", &bash));
        assert!(!blocks_for_watch("claude", "Notification", &bash));
        assert!(!blocks_for_watch("copilot", "permissionRequest", &bash));
        let (line, event, _, wait) =
            build_line("claude", "PermissionRequest", &bash.to_string(), false);
        assert_eq!(event, "PermissionRequest");
        assert!(wait > 0);
        let v: Value = serde_json::from_str(&line).unwrap();
        assert_eq!(v["await"]["wait_ms"].as_u64(), Some(wait));
        let (line, _, _, wait) = build_line("claude", "Stop", "{}", false);
        assert_eq!(wait, 0);
        assert!(serde_json::from_str::<Value>(&line)
            .unwrap()
            .get("await")
            .is_none());
    }

    #[test]
    fn parent_lookup_and_pipe_name() {
        assert_eq!(parent_of("4 0\n10 4\n20 10\n", 20), Some(10));
        assert_eq!(parent_of("4 0\n", 99), None);
        let n = pipe_name(std::path::Path::new("C:\\Users\\a\\.agentlink\\hook.sock"));
        assert!(n.starts_with(r"\\.\pipe\agentlink-"));
        assert_eq!(
            n,
            pipe_name(std::path::Path::new("c:\\users\\a\\.agentlink\\hook.sock"))
        );
    }

    fn scratch(name: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("agentlink-hook-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(d.join(".git")).unwrap();
        d
    }

    #[test]
    fn remote_spec_accepts_only_loopback() {
        assert_eq!(
            parse_remote("tcp:127.0.0.1:47533"),
            Some(("127.0.0.1", 47533))
        );
        assert_eq!(parse_remote("tcp:localhost:1"), Some(("localhost", 1)));
        assert_eq!(parse_remote("tcp:[::1]:9"), Some(("::1", 9)));
        for bad in [
            "tcp:0.0.0.0:1",
            "tcp:10.0.0.5:1",
            "tcp:example.com:1",
            "tcp:127.0.0.1",
            "tcp:127.0.0.1:",
            "tcp:127.0.0.1:0",
            "tcp:::1:9",
            "udp:127.0.0.1:1",
            "",
        ] {
            assert_eq!(parse_remote(bad), None, "{bad}");
        }
    }

    #[test]
    fn auth_line_shape() {
        let v: Value = serde_json::from_str(&auth_line("tok", "PC1", "win")).unwrap();
        assert_eq!(v, json!({"auth": "tok", "host": "PC1", "os": "win"}));
    }

    #[test]
    fn branch_from_git_head() {
        let d = scratch("branch");
        std::fs::write(d.join(".git/HEAD"), "ref: refs/heads/feat/x\n").unwrap();
        std::fs::create_dir_all(d.join("sub/dir")).unwrap();
        let sub = d.join("sub/dir");
        assert_eq!(branch_of(&sub.to_string_lossy()).as_deref(), Some("feat/x"));
        std::fs::write(d.join(".git/HEAD"), "0123456789abcdef\n").unwrap();
        assert_eq!(branch_of(&sub.to_string_lossy()), None);
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn remote_line_carries_payload_cwd_branch() {
        let d = scratch("line");
        std::fs::write(d.join(".git/HEAD"), "ref: refs/heads/main\n").unwrap();
        let payload = json!({"cwd": d.to_string_lossy()}).to_string();
        let v: Value = serde_json::from_str(&build_line("copilot", "x", &payload, true).0).unwrap();
        assert_eq!(v["branch"], "main");
        let v: Value =
            serde_json::from_str(&build_line("copilot", "x", &payload, false).0).unwrap();
        assert!(v.get("branch").is_none());
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn tcp_sends_auth_then_line_and_reads_reply() {
        use std::io::Write;
        let l = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let port = l.local_addr().unwrap().port();
        let srv = std::thread::spawn(move || {
            let (mut s, _) = l.accept().unwrap();
            let mut r = std::io::BufReader::new(s.try_clone().unwrap());
            let (mut a, mut b) = (String::new(), String::new());
            r.read_line(&mut a).unwrap();
            r.read_line(&mut b).unwrap();
            s.write_all(b"{\"decision\":\"allow\"}\n").unwrap();
            (a, b)
        });
        let lines = [json!({"agent": "claude"}).to_string()];
        let reply = send_tcp(
            ("127.0.0.1", port),
            auth_line("tok", "h", "linux"),
            &lines,
            Some(Duration::from_secs(2)),
        );
        assert_eq!(reply.as_deref(), Some("{\"decision\":\"allow\"}"));
        let (a, b) = srv.join().unwrap();
        assert!(a.contains("\"auth\":\"tok\""));
        assert_eq!(b.trim(), lines[0]);
    }
}
