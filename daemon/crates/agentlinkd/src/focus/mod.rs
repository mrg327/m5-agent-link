//! Bringing the agent's terminal to the front (DESIGN §10).
//!
//! Everything in `HostCtx` is relayed from a hook process through a device, so it is
//! treated as untrusted: values are validated here before use, external tools are run
//! with `Command` args (never a shell) under a timeout, and AppleScript receives values
//! as `argv`, never interpolated into the script.

mod generic;
mod kitty;
mod macos_terminal;
pub mod scroll;
mod tmux;
#[cfg(windows)]
mod windows_terminal;

use crate::registry::HostCtx;
use agentlink_proto::Agent;
use async_trait::async_trait;
use std::io::Read;
use std::process::{Command, Stdio};
use std::sync::Arc;
use std::time::{Duration, Instant};

pub struct FocusTarget {
    pub sid: u64,
    pub agent: Option<Agent>,
    pub ctx: HostCtx,
}

#[derive(Debug)]
pub enum FocusError {
    NoWindow,
    Unsupported,
    Internal(String),
}

#[async_trait]
pub trait FocusDriver: Send + Sync {
    async fn focus(&self, target: &FocusTarget) -> Result<(), FocusError>;
}

// ---- validation of untrusted identity values ----

fn all(s: &str, f: impl Fn(char) -> bool) -> bool {
    !s.is_empty() && s.chars().all(f)
}

fn digits(s: &str, max_len: usize) -> bool {
    all(s, |c| c.is_ascii_digit()) && s.len() <= max_len
}

/// `/dev/ttysNNN` (macOS) or `/dev/pts/N` (Linux).
pub fn valid_tty(s: &str) -> bool {
    s.strip_prefix("/dev/pts/")
        .or_else(|| s.strip_prefix("/dev/ttys"))
        .or_else(|| s.strip_prefix("/dev/tty"))
        .is_some_and(|d| digits(d, 6))
}

/// kitty window id: plain decimal.
pub fn valid_kitty_id(s: &str) -> bool {
    digits(s, 10)
}

/// `unix:/path`, `unix:@abstract`, or `tcp:localhost:PORT` / `tcp:127.0.0.1:PORT`.
pub fn valid_listen_on(s: &str) -> bool {
    if let Some(p) = s.strip_prefix("unix:") {
        return (p.starts_with('/') || p.starts_with('@'))
            && all(p, |c| c.is_ascii_alphanumeric() || "/._@-+".contains(c))
            && !p.contains("..");
    }
    s.strip_prefix("tcp:localhost:")
        .or_else(|| s.strip_prefix("tcp:127.0.0.1:"))
        .is_some_and(|p| digits(p, 5))
}

/// tmux pane id, e.g. `%12`.
pub fn valid_tmux_pane(s: &str) -> bool {
    s.strip_prefix('%').is_some_and(|d| digits(d, 10))
}

// ---- running external tools ----

pub const CMD_TIMEOUT: Duration = Duration::from_secs(3);

pub struct Out {
    pub ok: bool,
    pub stdout: String,
    pub stderr: String,
}

/// Runs `prog args` without a shell, killing it after `CMD_TIMEOUT`. A missing binary is
/// `Unsupported`; a timeout is an `Internal` error.
pub fn run(prog: &str, args: &[String]) -> Result<Out, FocusError> {
    let mut child = Command::new(prog)
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| match e.kind() {
            std::io::ErrorKind::NotFound => FocusError::Unsupported,
            _ => FocusError::Internal(format!("{prog}: {e}")),
        })?;
    fn drain(mut r: impl Read + Send + 'static) -> std::thread::JoinHandle<String> {
        std::thread::spawn(move || {
            let mut b = Vec::new();
            let _ = r.by_ref().take(1 << 16).read_to_end(&mut b);
            String::from_utf8_lossy(&b).into_owned()
        })
    }
    let so = drain(child.stdout.take().expect("piped"));
    let se = drain(child.stderr.take().expect("piped"));
    let start = Instant::now();
    let status = loop {
        match child.try_wait() {
            Ok(Some(s)) => break s,
            Ok(None) if start.elapsed() < CMD_TIMEOUT => {
                std::thread::sleep(Duration::from_millis(2))
            }
            Ok(None) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(FocusError::Internal(format!("{prog} timed out")));
            }
            Err(e) => return Err(FocusError::Internal(format!("{prog}: {e}"))),
        }
    };
    Ok(Out {
        ok: status.success(),
        stdout: so.join().unwrap_or_default(),
        stderr: se.join().unwrap_or_default(),
    })
}

pub fn args<const N: usize>(a: [&str; N]) -> Vec<String> {
    a.iter().map(|s| s.to_string()).collect()
}

// ---- dispatch ----

/// The terminal (outer window) that has to come to the front.
#[derive(Debug, PartialEq, Eq)]
pub enum Outer {
    Kitty,
    AppleTerminal,
    WindowsTerminal,
    /// macOS GUI app to activate by name (`open -a`).
    App(&'static str),
    /// Raise by window id / pid via xdotool.
    Linux,
    Unknown,
}

pub fn env<'a>(ctx: &'a HostCtx, k: &str) -> Option<&'a str> {
    ctx.env.get(k).map(String::as_str).filter(|v| !v.is_empty())
}

/// kitty's own variables, or its terminfo name (some agents scrub KITTY_* and TERM_PROGRAM
/// from hook environments but keep TERM).
fn is_kitty(ctx: &HostCtx) -> bool {
    env(ctx, "KITTY_WINDOW_ID").is_some() || env(ctx, "TERM") == Some("xterm-kitty")
}

/// Picks the outer terminal driver from the captured identity. Pure.
pub fn select_outer(ctx: &HostCtx, os: &str) -> Outer {
    let term = env(ctx, "TERM_PROGRAM");
    match os {
        "macos" => match term {
            Some("Apple_Terminal") => Outer::AppleTerminal,
            Some("kitty") => Outer::Kitty,
            Some("iTerm.app") => Outer::App("iTerm"),
            Some("vscode") => Outer::App("Visual Studio Code"),
            Some("WezTerm") => Outer::App("WezTerm"),
            Some("ghostty") => Outer::App("Ghostty"),
            Some("Hyper") => Outer::App("Hyper"),
            // tmux and screen overwrite TERM_PROGRAM; fall back to what they left alone.
            _ if is_kitty(ctx) => Outer::Kitty,
            _ => Outer::Unknown,
        },
        "windows" if env(ctx, "WT_SESSION").is_some() => Outer::WindowsTerminal,
        "linux" => {
            if term == Some("kitty") || is_kitty(ctx) {
                Outer::Kitty
            } else {
                Outer::Linux
            }
        }
        _ => Outer::Unknown,
    }
}

struct SystemFocus;

#[async_trait]
impl FocusDriver for SystemFocus {
    async fn focus(&self, t: &FocusTarget) -> Result<(), FocusError> {
        tracing::debug!("focus sid={} agent={:?}", t.sid, t.agent);
        let ctx = t.ctx.clone();
        tokio::task::spawn_blocking(move || focus_blocking(&ctx, std::env::consts::OS))
            .await
            .map_err(|e| FocusError::Internal(e.to_string()))?
    }
}

fn focus_blocking(ctx: &HostCtx, os: &str) -> Result<(), FocusError> {
    let pane = tmux::pane(ctx);
    let client_ttys = match pane {
        Some(p) => tmux::select(ctx, p)?,
        None => Vec::new(),
    };
    let outer = select_outer(ctx, os);
    tracing::debug!("focus outer={outer:?} tmux={}", pane.is_some());
    let res = match outer {
        Outer::Kitty => kitty::focus(ctx, os),
        Outer::AppleTerminal => macos_terminal::focus(ctx.tty.as_deref()),
        Outer::App(name) => generic::activate_app(name),
        Outer::Linux => generic::linux(ctx),
        #[cfg(windows)]
        Outer::WindowsTerminal => windows_terminal::focus(ctx),
        #[cfg(not(windows))]
        Outer::WindowsTerminal => Err(FocusError::Unsupported),
        Outer::Unknown if pane.is_some() && os == "macos" => tmux::outer_macos(&client_ttys),
        Outer::Unknown => Err(FocusError::Unsupported),
    };
    match res {
        // The pane is already selected; failing to find the window around it is not fatal.
        Err(FocusError::Unsupported | FocusError::NoWindow) if pane.is_some() => Ok(()),
        r => r,
    }
}

/// The driver the daemon uses. Picks per-OS/per-terminal drivers (DESIGN §10).
pub fn default_driver() -> Arc<dyn FocusDriver> {
    Arc::new(SystemFocus)
}

pub use scroll::{default_driver as default_scroll_driver, ScrollDriver};

#[cfg(test)]
mod tests {
    use super::*;

    pub(super) fn ctx(env: &[(&str, &str)]) -> HostCtx {
        HostCtx {
            env: env
                .iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
            ..Default::default()
        }
    }

    #[test]
    fn dispatch_by_terminal_and_os() {
        let sel = |e: &[(&str, &str)], os| select_outer(&ctx(e), os);
        assert_eq!(sel(&[("TERM_PROGRAM", "kitty")], "macos"), Outer::Kitty);
        assert_eq!(
            sel(&[("TERM_PROGRAM", "Apple_Terminal")], "macos"),
            Outer::AppleTerminal
        );
        // inside tmux TERM_PROGRAM is "tmux"; kitty's own variable still identifies the outer
        assert_eq!(
            sel(
                &[("TERM_PROGRAM", "tmux"), ("KITTY_WINDOW_ID", "3")],
                "macos"
            ),
            Outer::Kitty
        );
        assert_eq!(
            sel(&[("TERM_PROGRAM", "iTerm.app")], "macos"),
            Outer::App("iTerm")
        );
        assert_eq!(sel(&[("TERM_PROGRAM", "tmux")], "macos"), Outer::Unknown);
        assert_eq!(
            sel(&[("WT_SESSION", "x")], "windows"),
            Outer::WindowsTerminal
        );
        assert_eq!(sel(&[("WT_SESSION", "x")], "macos"), Outer::Unknown);
        assert_eq!(sel(&[], "linux"), Outer::Linux);
        assert_eq!(sel(&[("KITTY_WINDOW_ID", "")], "macos"), Outer::Unknown);
        assert_eq!(sel(&[("TERM", "xterm-kitty")], "macos"), Outer::Kitty);
    }

    #[test]
    fn validators_accept_real_values_and_reject_injection() {
        assert!(valid_tty("/dev/ttys003") && valid_tty("/dev/pts/12") && valid_tty("/dev/tty1"));
        for bad in [
            "",
            "/dev/ttys",
            "/dev/ttys3; rm -rf /",
            "/dev/ttys3\n",
            "/dev/../etc/passwd",
            "-e",
            "/dev/tty1/../x",
            "/dev/ttyS0",
        ] {
            assert!(!valid_tty(bad), "{bad:?}");
        }
        assert!(valid_kitty_id("12") && !valid_kitty_id("1;x") && !valid_kitty_id("-1"));
        assert!(!valid_kitty_id(""));
        for ok in [
            "unix:/tmp/kitty-123",
            "unix:@kitty",
            "tcp:localhost:5000",
            "tcp:127.0.0.1:80",
        ] {
            assert!(valid_listen_on(ok), "{ok}");
        }
        for bad in [
            "",
            "unix:",
            "unix:relative",
            "unix:/tmp/../etc",
            "unix:/tmp/a b",
            "unix:/tmp/a;b",
            "tcp:evil.com:80",
            "tcp:localhost:80x",
            "--password=x",
            "ssh:/tmp/x",
        ] {
            assert!(!valid_listen_on(bad), "{bad:?}");
        }
        assert!(valid_tmux_pane("%0") && valid_tmux_pane("%123"));
        for bad in ["", "%", "12", "%1;ls", "%-1", "%1 -t x", "$1"] {
            assert!(!valid_tmux_pane(bad), "{bad:?}");
        }
    }

    #[test]
    fn run_reports_missing_binary_and_captures_output() {
        assert!(matches!(
            run("/nonexistent/agentlink-tool", &[]),
            Err(FocusError::Unsupported)
        ));
        let o = run("echo", &args(["hi"])).unwrap();
        assert!(o.ok && o.stdout.trim() == "hi");
    }

    /// Manual: `AL_FOCUS_TTY=/dev/ttysNNN AL_FOCUS_ENV=TERM_PROGRAM=Apple_Terminal cargo test -p agentlinkd live_focus -- --ignored`
    #[test]
    #[ignore]
    fn live_focus() {
        let pairs: Vec<(String, String)> = std::env::var("AL_FOCUS_ENV")
            .unwrap_or_default()
            .split_whitespace()
            .filter_map(|kv| kv.split_once('=').map(|(k, v)| (k.into(), v.into())))
            .collect();
        let refs: Vec<(&str, &str)> = pairs.iter().map(|(k, v)| (&k[..], &v[..])).collect();
        let mut c = ctx(&refs);
        c.tty = std::env::var("AL_FOCUS_TTY").ok();
        let r = focus_blocking(&c, std::env::consts::OS);
        println!("live focus result: {r:?}");
        r.unwrap();
    }
}
