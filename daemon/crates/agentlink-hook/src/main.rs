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
    json!({"agent": agent, "event": event, "ts": ts, "cwd": cwd, "ppid": ppid, "tty": tty, "pids": pids, "env": env, "payload": payload})
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

/// Windows twin of the unix `send`: one line over the named pipe, then an optional reply
/// read on a helper thread so a silent daemon cannot hold us past `REPLY_TIMEOUT`.
#[cfg(windows)]
fn send(line: &str, want_reply: bool) -> Option<String> {
    use std::io::{BufRead, BufReader, Write};

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
    pipe.write_all(line.as_bytes()).ok()?;
    pipe.write_all(b"\n").ok()?;
    pipe.flush().ok()?;
    if !want_reply {
        return None;
    }
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let mut reply = String::new();
        let _ = BufReader::new(pipe).read_line(&mut reply);
        let _ = tx.send(reply);
    });
    let reply = rx.recv_timeout(REPLY_TIMEOUT).ok()?;
    Some(reply.trim().to_owned()).filter(|r| !r.is_empty())
}

#[cfg(not(any(unix, windows)))]
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
}
