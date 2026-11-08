//! tmux: select the agent's window and pane, then report the attached clients' ttys so the
//! terminal hosting them can be raised. Also drives the pane's scrollback (copy mode) for the
//! device's SCROLL mode.

use super::scroll::tidy_preview;
use super::{args, env, run, valid_tmux_pane, valid_tty, FocusError, HostCtx};
use agentlink_proto::{ScrollPos, ScrollUnit};

pub fn pane(ctx: &HostCtx) -> Option<&str> {
    env(ctx, "TMUX")?;
    env(ctx, "TMUX_PANE").filter(|p| valid_tmux_pane(p))
}

/// Socket path from `$TMUX` (`socket,server-pid,session`), when it looks sane.
fn socket(ctx: &HostCtx) -> Option<&str> {
    let s = env(ctx, "TMUX")?.split(',').next()?;
    (s.starts_with('/')
        && !s.contains("..")
        && s.chars()
            .all(|c| c.is_ascii_alphanumeric() || "/._-+".contains(c)))
    .then_some(s)
}

fn cmd(ctx: &HostCtx, rest: &[&str]) -> Vec<String> {
    let mut a = Vec::new();
    if let Some(s) = socket(ctx) {
        a.extend(args(["-S", s]));
    }
    a.extend(rest.iter().map(|s| s.to_string()));
    a
}

/// Selects the pane's window and the pane. Returns ttys of clients attached to its session.
pub fn select(ctx: &HostCtx, pane: &str) -> Result<Vec<String>, FocusError> {
    for sub in ["select-window", "select-pane"] {
        let out = run("tmux", &cmd(ctx, &[sub, "-t", pane]))?;
        if !out.ok {
            return Err(if out.stderr.contains("can't find") {
                FocusError::NoWindow
            } else {
                FocusError::Internal(format!("tmux {sub}: {}", out.stderr.trim()))
            });
        }
    }
    let sess = run(
        "tmux",
        &cmd(ctx, &["display-message", "-p", "-t", pane, "#{session_id}"]),
    )
    .ok()
    .filter(|o| o.ok)
    .map(|o| o.stdout.trim().to_owned())
    .filter(|s| {
        s.strip_prefix('$')
            .is_some_and(|d| d.chars().all(|c| c.is_ascii_digit()) && !d.is_empty())
    });
    let Some(sess) = sess else {
        return Ok(Vec::new());
    };
    let clients = run(
        "tmux",
        &cmd(ctx, &["list-clients", "-t", &sess, "-F", "#{client_tty}"]),
    )
    .ok()
    .filter(|o| o.ok);
    Ok(clients
        .map(|o| {
            o.stdout
                .lines()
                .map(str::trim)
                .filter(|t| valid_tty(t))
                .map(str::to_owned)
                .collect()
        })
        .unwrap_or_default())
}

// ---- scroll (UI-V2 §6.2) ----

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Pos {
    pub in_mode: bool,
    pub scroll_position: u64,
    pub history_size: u64,
    pub height: u64,
}

pub const POS_FORMAT: &str = "#{pane_in_mode} #{scroll_position} #{history_size} #{pane_height}";

/// Parses `display-message -p` output for `POS_FORMAT`; `scroll_position` is empty outside copy mode.
pub fn parse_pos(s: &str) -> Option<Pos> {
    let f: Vec<&str> = s.trim_end_matches(['\r', '\n']).split(' ').collect();
    if f.len() != 4 {
        return None;
    }
    let num = |x: &str| -> Option<u64> {
        if x.is_empty() {
            Some(0)
        } else {
            x.parse().ok()
        }
    };
    Some(Pos {
        in_mode: f[0] == "1",
        scroll_position: num(f[1])?,
        history_size: num(f[2])?,
        height: num(f[3])?,
    })
}

/// The copy-mode command for a move, as the tail of `send-keys -t <pane> -X ...`. `None` when
/// nothing has to be sent.
pub fn scroll_keys(unit: ScrollUnit, delta: i64) -> Option<Vec<String>> {
    let n = delta.unsigned_abs().to_string();
    match (unit, delta.signum()) {
        (_, 0) => None,
        (ScrollUnit::Lines, s) => Some(args([
            "-N",
            &n,
            if s < 0 { "scroll-up" } else { "scroll-down" },
        ])),
        (ScrollUnit::Pages, s) => Some(args([
            "-N",
            &n,
            if s < 0 { "page-up" } else { "page-down" },
        ])),
        (ScrollUnit::ToEnd, s) => Some(args([if s < 0 { "history-top" } else { "cancel" }])),
    }
}

/// `capture-pane` arguments for exactly the top visible line when the pane is scrolled back by
/// `scroll_position` lines (negative start/end lines count into the history).
pub fn capture_args(pane: &str, scroll_position: u64) -> Vec<String> {
    let at = if scroll_position == 0 {
        "0".to_owned()
    } else {
        format!("-{scroll_position}")
    };
    let mut a = args(["capture-pane", "-p", "-t", pane, "-S"]);
    a.push(at.clone());
    a.push("-E".to_owned());
    a.push(at);
    a
}

fn tmux_ok(ctx: &HostCtx, rest: &[String]) -> Result<super::Out, FocusError> {
    let refs: Vec<&str> = rest.iter().map(String::as_str).collect();
    let out = run("tmux", &cmd(ctx, &refs))?;
    if !out.ok {
        return Err(if out.stderr.contains("can't find") {
            FocusError::NoWindow
        } else {
            FocusError::Internal(format!(
                "tmux {}: {}",
                refs.first().unwrap_or(&"?"),
                out.stderr.trim()
            ))
        });
    }
    Ok(out)
}

fn query(ctx: &HostCtx, pane: &str) -> Result<Pos, FocusError> {
    let out = tmux_ok(
        ctx,
        &args(["display-message", "-p", "-t", pane, POS_FORMAT]),
    )?;
    parse_pos(&out.stdout)
        .ok_or_else(|| FocusError::Internal(format!("tmux: odd position {:?}", out.stdout.trim())))
}

fn send_x(ctx: &HostCtx, pane: &str, tail: &[String]) -> Result<(), FocusError> {
    let mut a = args(["send-keys", "-t", pane, "-X"]);
    a.extend(tail.iter().cloned());
    tmux_ok(ctx, &a).map(|_| ())
}

/// Moves the pane's view in copy mode and reports where it is. Scrolling down to the live bottom
/// leaves copy mode, as tmux's own mouse wheel does, so the agent's output flows again.
pub fn scroll(
    ctx: &HostCtx,
    pane: &str,
    unit: ScrollUnit,
    delta: i64,
    preview: bool,
) -> Result<ScrollPos, FocusError> {
    let before = query(ctx, pane)?;
    let up = delta < 0;
    match scroll_keys(unit, delta) {
        None => {}
        // already live: a move down or "to bottom" has nothing to do
        Some(_) if !before.in_mode && !up => {}
        Some(keys) => {
            if !before.in_mode {
                tmux_ok(ctx, &args(["copy-mode", "-t", pane]))?;
            }
            send_x(ctx, pane, &keys)?;
        }
    }
    let mut after = query(ctx, pane)?;
    if after.in_mode && after.scroll_position == 0 && !up {
        send_x(ctx, pane, &args(["cancel"]))?;
        after.in_mode = false;
    }
    let lines_above = if after.in_mode {
        after.scroll_position
    } else {
        0
    };
    let preview = if preview {
        tmux_ok(ctx, &capture_args(pane, lines_above))
            .ok()
            .and_then(|o| o.stdout.lines().next().map(tidy_preview))
            .filter(|p| !p.is_empty())
    } else {
        None
    };
    Ok(ScrollPos {
        lines_above,
        at_bottom: lines_above == 0,
        total: Some(after.history_size),
        preview,
    })
}

/// Terminal.app hosts the tmux client whose tty it knows.
pub fn outer_macos(client_ttys: &[String]) -> Result<(), FocusError> {
    for tty in client_ttys {
        if super::macos_terminal::focus(Some(tty)).is_ok() {
            return Ok(());
        }
    }
    Err(FocusError::NoWindow)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctx(pairs: &[(&str, &str)]) -> HostCtx {
        HostCtx {
            env: pairs
                .iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
            ..Default::default()
        }
    }

    #[test]
    fn needs_tmux_and_a_valid_pane() {
        assert_eq!(
            pane(&ctx(&[("TMUX", "/t/s,1,0"), ("TMUX_PANE", "%4")])),
            Some("%4")
        );
        assert_eq!(pane(&ctx(&[("TMUX_PANE", "%4")])), None);
        assert_eq!(
            pane(&ctx(&[("TMUX", "/t/s,1,0"), ("TMUX_PANE", "%4;x")])),
            None
        );
    }

    #[test]
    fn builds_copy_mode_moves_and_parses_positions() {
        assert_eq!(
            scroll_keys(ScrollUnit::Lines, -3).unwrap(),
            ["-N", "3", "scroll-up"]
        );
        assert_eq!(
            scroll_keys(ScrollUnit::Lines, 9).unwrap(),
            ["-N", "9", "scroll-down"]
        );
        assert_eq!(
            scroll_keys(ScrollUnit::Pages, -1).unwrap(),
            ["-N", "1", "page-up"]
        );
        assert_eq!(
            scroll_keys(ScrollUnit::Pages, 2).unwrap(),
            ["-N", "2", "page-down"]
        );
        assert_eq!(scroll_keys(ScrollUnit::ToEnd, -1).unwrap(), ["history-top"]);
        assert_eq!(scroll_keys(ScrollUnit::ToEnd, 1).unwrap(), ["cancel"]);
        assert_eq!(scroll_keys(ScrollUnit::Lines, 0), None);
        assert_eq!(
            parse_pos("1 142 1200 50\n"),
            Some(Pos {
                in_mode: true,
                scroll_position: 142,
                history_size: 1200,
                height: 50
            })
        );
        // outside copy mode tmux expands #{scroll_position} to nothing
        assert_eq!(
            parse_pos("0  1200 50\n"),
            Some(Pos {
                in_mode: false,
                scroll_position: 0,
                history_size: 1200,
                height: 50
            })
        );
        assert_eq!(parse_pos("garbage"), None);
        assert_eq!(parse_pos("1 x 1 1"), None);
        assert_eq!(
            capture_args("%4", 142),
            ["capture-pane", "-p", "-t", "%4", "-S", "-142", "-E", "-142"]
        );
        assert_eq!(
            capture_args("%4", 0),
            ["capture-pane", "-p", "-t", "%4", "-S", "0", "-E", "0"]
        );
    }

    #[test]
    fn socket_comes_from_tmux_env_only_when_safe() {
        let c = ctx(&[("TMUX", "/private/tmp/tmux-501/default,123,0")]);
        assert_eq!(
            cmd(&c, &["a"]),
            ["-S", "/private/tmp/tmux-501/default", "a"]
        );
        let c = ctx(&[("TMUX", "-oEvil=1,1,0")]);
        assert_eq!(cmd(&c, &["a"]), ["a"]);
    }
}
