//! `agentlink ssh <dest>`: registers this terminal window with the daemon, then runs ssh with
//! a reverse tunnel to the daemon's remote socket. The registration lives as long as the
//! hook.sock connection, so it is held open until ssh exits.

use anyhow::Result;

#[cfg(unix)]
pub fn run(port: u16, dest: &str, pre: &[String], extra: &[String]) -> Result<()> {
    use anyhow::{bail, Context};
    use std::io::{BufRead, BufReader, Write};
    use std::os::unix::net::UnixStream;
    use std::time::Duration;

    let path = crate::home().join("hook.sock");
    let mut s = UnixStream::connect(&path).with_context(|| {
        format!(
            "agentlinkd is not running (cannot connect to {})",
            path.display()
        )
    })?;
    s.set_read_timeout(Some(Duration::from_secs(3)))?;
    writeln!(s, "{}", attach_request(&capture_ctx()))?;
    let mut line = String::new();
    BufReader::new(&s)
        .read_line(&mut line)
        .context("no reply from daemon")?;
    let reply: serde_json::Value = serde_json::from_str(&line).context("bad reply from daemon")?;
    if reply["ok"].as_bool() != Some(true) {
        bail!(
            "{}",
            reply["error"]
                .as_str()
                .unwrap_or("daemon refused the attach")
        );
    }
    let Some(socket) = reply["socket"].as_str() else {
        bail!("daemon reply has no socket");
    };
    eprintln!("agentlink: tunnel 127.0.0.1:{port} on {dest} → this watch (window registered)");
    let bin = std::env::var_os("AGENTLINK_SSH").unwrap_or_else(|| "ssh".into());
    let status = std::process::Command::new(&bin)
        .args(ssh_argv(port, socket, pre, dest, extra))
        .status()
        .with_context(|| format!("cannot run {}", bin.to_string_lossy()))?;
    drop(s);
    std::process::exit(exit_code(status));
}

#[cfg(not(unix))]
pub fn run(_port: u16, _dest: &str, _pre: &[String], _extra: &[String]) -> Result<()> {
    anyhow::bail!("run this on the Mac that runs agentlinkd")
}

/// ssh's exit code, or 128+signal when it was killed.
#[cfg(unix)]
fn exit_code(st: std::process::ExitStatus) -> i32 {
    use std::os::unix::process::ExitStatusExt;
    st.code().unwrap_or_else(|| 128 + st.signal().unwrap_or(0))
}

/// `pre` goes before the destination (ssh options), `extra` after it (options or a command).
fn ssh_argv(port: u16, socket: &str, pre: &[String], dest: &str, extra: &[String]) -> Vec<String> {
    let mut v = vec![
        "-o".into(),
        "ExitOnForwardFailure=yes".into(),
        "-R".into(),
        format!("127.0.0.1:{port}:{socket}"),
    ];
    v.extend_from_slice(pre);
    v.push(dest.to_owned());
    v.extend_from_slice(extra);
    v
}

fn attach_request(ctx: &serde_json::Value) -> serde_json::Value {
    serde_json::json!({"ctl": "remote_attach", "ctx": ctx})
}

/// Env that identifies the terminal/window. Keep in sync with `ENV_KEYS` in agentlink-hook.
#[cfg_attr(not(unix), allow(dead_code))]
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

/// Walks up from `start` through `ps` rows (`pid ppid tty`): up to `max` pids (nearest
/// first) and the first real controlling tty. Same as the hook's `walk_chain`.
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

/// This terminal's context, shaped like the hook's `cwd`/`env`/`tty`/`pids`.
#[cfg(unix)]
fn capture_ctx() -> serde_json::Value {
    let env: serde_json::Map<String, serde_json::Value> = ENV_KEYS
        .iter()
        .filter_map(|k| Some((k.to_string(), std::env::var(k).ok()?.into())))
        .collect();
    let cwd = std::env::current_dir()
        .ok()
        .map(|p| p.to_string_lossy().into_owned());
    let ps = std::process::Command::new("ps")
        .args(["-A", "-o", "pid=,ppid=,tty="])
        .stdin(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).into_owned())
        .unwrap_or_default();
    let me = std::process::id();
    let (mut pids, tty) = walk_chain(&ps, me, 8);
    if pids.is_empty() {
        pids = vec![me];
    }
    serde_json::json!({"cwd": cwd, "env": env, "tty": tty, "pids": pids})
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn argv_keeps_extra_args_after_dest_untouched() {
        let extra = vec!["-p".to_owned(), "2222".into(), "--".into(), "ls".into()];
        let v = ssh_argv(
            47533,
            "/h/.agentlink/remote.sock",
            &["-t".to_owned()],
            "win",
            &extra,
        );
        assert_eq!(
            v,
            [
                "-o",
                "ExitOnForwardFailure=yes",
                "-R",
                "127.0.0.1:47533:/h/.agentlink/remote.sock",
                "-t",
                "win",
                "-p",
                "2222",
                "--",
                "ls"
            ]
        );
    }

    #[test]
    fn attach_request_shape_and_chain_starts_at_self() {
        let r = attach_request(&serde_json::json!({"pids": [3, 2]}));
        assert_eq!(r["ctl"], "remote_attach");
        assert_eq!(r["ctx"]["pids"], serde_json::json!([3, 2]));
        let (c, tty) = walk_chain("30 20 ttys001\n20 1 ??\n", 30, 8);
        assert_eq!((c, tty.as_deref()), (vec![30, 20], Some("/dev/ttys001")));
    }
}
