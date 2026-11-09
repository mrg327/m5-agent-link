//! kitty: remote control selects (and on macOS raises) the OS window/tab/split, and drives its
//! scrollback (`scroll-window`, `ls`, `get-text`) for the device's SCROLL mode.

use super::scroll::{tidy_preview, Tracked};
use super::{args, env, generic, run, valid_kitty_id, valid_listen_on, FocusError, HostCtx};
use agentlink_proto::{ScrollPos, ScrollUnit};

const MAC_BIN: &str = "/Applications/kitty.app/Contents/MacOS/kitty";

const NEEDS_REMOTE_CONTROL: &str = "kitty remote control is not enabled: add \
    `allow_remote_control socket-only` and `listen_on unix:/tmp/kitty` to kitty.conf and \
    restart kitty (only the app was raised)";

pub fn focus_args(listen_on: &str, matcher: &str) -> Vec<String> {
    let mut a = args(["@", "--to"]);
    a.push(listen_on.to_owned());
    a.extend(args(["focus-window", "--match"]));
    a.push(matcher.to_owned());
    a
}

/// `id:N` from KITTY_WINDOW_ID, else the hook's pid chain: kitty's `pid:` matches the
/// window's child process (the shell, or `login` on macOS), which is one of the ancestors.
/// Some agents scrub KITTY_* from hook environments, so the chain is the common path.
pub fn window_match(ctx: &HostCtx) -> Option<String> {
    if let Some(id) = env(ctx, "KITTY_WINDOW_ID").filter(|i| valid_kitty_id(i)) {
        return Some(format!("id:{id}"));
    }
    let m: Vec<String> = ctx
        .pids
        .iter()
        .take(8)
        .map(|p| format!("pid:{p}"))
        .collect();
    (!m.is_empty()).then(|| m.join(" or "))
}

/// KITTY_LISTEN_ON, else the socket kitty creates for `listen_on unix:/tmp/kitty`
/// (`/tmp/kitty-<kitty pid>`), where the kitty pid is KITTY_PID or an ancestor in the chain.
pub fn socket(ctx: &HostCtx, exists: impl Fn(&str) -> bool) -> Option<String> {
    if let Some(l) = env(ctx, "KITTY_LISTEN_ON").filter(|l| valid_listen_on(l)) {
        return Some(l.to_owned());
    }
    let kitty_pid = env(ctx, "KITTY_PID").and_then(|p| p.parse::<u32>().ok());
    kitty_pid
        .into_iter()
        .chain(ctx.pids.iter().copied())
        .map(|p| format!("/tmp/kitty-{p}"))
        .find(|p| exists(p))
        .map(|p| format!("unix:{p}"))
}

pub fn focus(ctx: &HostCtx, os: &str) -> Result<(), FocusError> {
    let listen = socket(ctx, |p| std::path::Path::new(p).exists());
    let (Some(matcher), Some(listen)) = (window_match(ctx), listen) else {
        let _ = raise(ctx, os);
        return Err(FocusError::Internal(NEEDS_REMOTE_CONTROL.into()));
    };
    let a = focus_args(&listen, &matcher);
    let out = match run("kitty", &a) {
        Err(FocusError::Unsupported) => run(MAC_BIN, &a)?,
        r => r?,
    };
    if !out.ok {
        let _ = raise(ctx, os);
        if out.stderr.contains("No matching windows") {
            return Err(FocusError::NoWindow);
        }
        return Err(FocusError::Internal(format!(
            "kitty focus-window failed: {}",
            out.stderr.trim()
        )));
    }
    // On macOS focus-window also activates the right kitty instance; `open -a kitty`
    // would pick an arbitrary one when several run, so it is only a fallback.
    if os == "macos" {
        return Ok(());
    }
    raise(ctx, os)
}

// ---- scroll (UI-V2 §6.2) ----

/// kitty's `SCROLL_AMOUNT`: `3l-` is three lines up (older), `2p+` two pages down, `start` / `end`
/// the ends of the scrollback. `None` when there is nothing to move.
pub fn scroll_amount(unit: ScrollUnit, delta: i64) -> Option<String> {
    let sign = if delta < 0 { "-" } else { "+" };
    match unit {
        ScrollUnit::ToEnd => Some(if delta < 0 { "start" } else { "end" }.to_owned()),
        _ if delta == 0 => None,
        ScrollUnit::Lines => Some(format!("{}l{sign}", delta.unsigned_abs())),
        ScrollUnit::Pages => Some(format!("{}p{sign}", delta.unsigned_abs())),
    }
}

fn rc(listen_on: &str, matcher: &str, sub: &str, tail: &[&str]) -> Vec<String> {
    let mut a = args(["@", "--to"]);
    a.push(listen_on.to_owned());
    a.push(sub.to_owned());
    a.extend(args(["--match"]));
    a.push(matcher.to_owned());
    a.extend(tail.iter().map(|s| s.to_string()));
    a
}

pub fn scroll_args(listen_on: &str, matcher: &str, amount: &str) -> Vec<String> {
    rc(listen_on, matcher, "scroll-window", &[amount])
}

pub fn ls_args(listen_on: &str, matcher: &str) -> Vec<String> {
    rc(listen_on, matcher, "ls", &[])
}

/// The whole scrollback plus screen as plain text, so the top visible line can be picked by
/// index from the known position. Only run when the device's privacy allows a preview.
pub fn get_text_args(listen_on: &str, matcher: &str) -> Vec<String> {
    rc(listen_on, matcher, "get-text", &["--extent", "all"])
}

/// From `kitty @ ls` JSON: the first matched window's `lines` (screen height) and, when kitty
/// reports it, `scrolled_by` (lines above the live bottom).
pub fn window_geometry(ls_json: &str) -> Option<(u64, Option<u64>)> {
    let v: serde_json::Value = serde_json::from_str(ls_json).ok()?;
    let w = v
        .as_array()?
        .iter()
        .filter_map(|os| os.get("tabs")?.as_array())
        .flatten()
        .filter_map(|t| t.get("windows")?.as_array())
        .flatten()
        .next()?;
    let lines = w.get("lines")?.as_u64()?;
    Some((lines, w.get("scrolled_by").and_then(|s| s.as_u64())))
}

/// The top visible line of a text dump (`get-text --extent all`) when `lines_above` lines of
/// scrollback sit above a screen of `height` lines, plus the scrollback size.
pub fn top_line(dump: &str, height: u64, lines_above: u64) -> (Option<&str>, u64) {
    let lines: Vec<&str> = dump
        .strip_suffix('\n')
        .unwrap_or(dump)
        .split('\n')
        .collect();
    let total = (lines.len() as u64).saturating_sub(height);
    let above = lines_above.min(total);
    let idx = (lines.len() as u64)
        .saturating_sub(height)
        .saturating_sub(above);
    (lines.get(idx as usize).copied(), total)
}

fn run_rc(a: &[String]) -> Result<super::Out, FocusError> {
    match run("kitty", a) {
        Err(FocusError::Unsupported) => run(MAC_BIN, a),
        r => r,
    }
}

pub fn scroll(
    ctx: &HostCtx,
    tracked: &Tracked,
    unit: ScrollUnit,
    delta: i64,
    preview: bool,
) -> Result<ScrollPos, FocusError> {
    let listen = socket(ctx, |p| std::path::Path::new(p).exists());
    let (Some(matcher), Some(listen)) = (window_match(ctx), listen) else {
        return Err(FocusError::Internal(NEEDS_REMOTE_CONTROL.into()));
    };
    if let Some(amount) = scroll_amount(unit, delta) {
        let out = run_rc(&scroll_args(&listen, &matcher, &amount))?;
        if !out.ok {
            if out.stderr.contains("No matching windows") {
                return Err(FocusError::NoWindow);
            }
            return Err(FocusError::Internal(format!(
                "kitty scroll-window failed: {}",
                out.stderr.trim()
            )));
        }
    }
    // kitty reports no scroll offset, so the daemon keeps its own estimate per window and
    // corrects it from `scrolled_by` or the text dump whenever either is available.
    let geometry = run_rc(&ls_args(&listen, &matcher))
        .ok()
        .filter(|o| o.ok)
        .and_then(|o| window_geometry(&o.stdout));
    let height = geometry.map(|g| g.0).unwrap_or(24).max(1);
    let mut total = None;
    let mut dump = None;
    if preview {
        if let Ok(o) = run_rc(&get_text_args(&listen, &matcher)) {
            if o.ok {
                total = Some(top_line(&o.stdout, height, 0).1);
                dump = Some(o.stdout);
            }
        }
    }
    let mut lines_above = match unit {
        ScrollUnit::Lines => tracked.apply(&matcher, delta, total),
        ScrollUnit::Pages => tracked.apply(&matcher, delta.saturating_mul(height as i64), total),
        ScrollUnit::ToEnd if delta > 0 => {
            tracked.set(&matcher, 0);
            0
        }
        ScrollUnit::ToEnd => {
            let top = total.unwrap_or_else(|| tracked.get(&matcher));
            tracked.set(&matcher, top);
            top
        }
    };
    if let Some((_, Some(reported))) = geometry {
        tracked.set(&matcher, reported);
        lines_above = reported;
    }
    let preview = dump
        .as_deref()
        .and_then(|d| top_line(d, height, lines_above).0)
        .map(tidy_preview)
        .filter(|p| !p.is_empty());
    Ok(ScrollPos {
        lines_above,
        at_bottom: lines_above == 0,
        total,
        preview,
    })
}

fn raise(ctx: &HostCtx, os: &str) -> Result<(), FocusError> {
    match os {
        "macos" => generic::activate_app("kitty"),
        "linux" => generic::linux(ctx),
        _ => Ok(()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_focus_window_args() {
        assert_eq!(
            focus_args("unix:/tmp/kitty-1", "id:7"),
            [
                "@",
                "--to",
                "unix:/tmp/kitty-1",
                "focus-window",
                "--match",
                "id:7"
            ]
        );
    }

    fn ctx(env: &[(&str, &str)], pids: &[u32]) -> HostCtx {
        HostCtx {
            env: env
                .iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
            pids: pids.to_vec(),
            ..Default::default()
        }
    }

    #[test]
    fn builds_scroll_amounts_and_commands() {
        assert_eq!(scroll_amount(ScrollUnit::Lines, -3).as_deref(), Some("3l-"));
        assert_eq!(
            scroll_amount(ScrollUnit::Lines, 12).as_deref(),
            Some("12l+")
        );
        assert_eq!(scroll_amount(ScrollUnit::Pages, -1).as_deref(), Some("1p-"));
        assert_eq!(scroll_amount(ScrollUnit::Pages, 2).as_deref(), Some("2p+"));
        assert_eq!(scroll_amount(ScrollUnit::ToEnd, 1).as_deref(), Some("end"));
        assert_eq!(
            scroll_amount(ScrollUnit::ToEnd, -1).as_deref(),
            Some("start")
        );
        assert_eq!(scroll_amount(ScrollUnit::Lines, 0), None);
        assert_eq!(
            scroll_args("unix:/tmp/kitty-1", "id:7", "3l-"),
            [
                "@",
                "--to",
                "unix:/tmp/kitty-1",
                "scroll-window",
                "--match",
                "id:7",
                "3l-"
            ]
        );
        assert_eq!(
            ls_args("unix:/tmp/kitty-1", "pid:10 or pid:20"),
            [
                "@",
                "--to",
                "unix:/tmp/kitty-1",
                "ls",
                "--match",
                "pid:10 or pid:20"
            ]
        );
        assert_eq!(
            get_text_args("unix:/tmp/k", "id:7"),
            [
                "@",
                "--to",
                "unix:/tmp/k",
                "get-text",
                "--match",
                "id:7",
                "--extent",
                "all"
            ]
        );
    }

    #[test]
    fn reads_geometry_from_ls_and_picks_the_top_line() {
        let ls = r#"[{"id":1,"tabs":[{"id":1,"windows":[{"id":7,"lines":3,"columns":80,"scrolled_by":2}]}]}]"#;
        assert_eq!(window_geometry(ls), Some((3, Some(2))));
        let ls = r#"[{"id":1,"tabs":[{"id":1,"windows":[{"id":7,"lines":52}]}]}]"#;
        assert_eq!(window_geometry(ls), Some((52, None)));
        assert_eq!(window_geometry("[]"), None);
        assert_eq!(window_geometry("not json"), None);
        // 5 lines of scrollback above a 3-line screen
        let dump = "s0\ns1\ns2\ns3\ns4\nv0\nv1\nv2\n";
        assert_eq!(top_line(dump, 3, 0), (Some("v0"), 5));
        assert_eq!(top_line(dump, 3, 2), (Some("s3"), 5));
        assert_eq!(top_line(dump, 3, 99), (Some("s0"), 5)); // clamped at the top
        assert_eq!(top_line("a\nb", 3, 0), (Some("a"), 0)); // shorter than a screen
    }

    #[test]
    fn matches_by_window_id_else_pid_chain() {
        let c = ctx(&[("KITTY_WINDOW_ID", "3")], &[10, 20]);
        assert_eq!(window_match(&c).as_deref(), Some("id:3"));
        let c = ctx(&[("KITTY_WINDOW_ID", "3;x")], &[10, 20]);
        assert_eq!(window_match(&c).as_deref(), Some("pid:10 or pid:20"));
        assert_eq!(window_match(&ctx(&[], &[])), None);
    }

    #[test]
    fn finds_socket_from_env_or_ancestor_pid() {
        let c = ctx(&[("KITTY_LISTEN_ON", "unix:/tmp/k-9")], &[10]);
        assert_eq!(socket(&c, |_| false).as_deref(), Some("unix:/tmp/k-9"));
        let c = ctx(&[("KITTY_LISTEN_ON", "unix:/tmp/a;b")], &[10, 47]);
        assert_eq!(
            socket(&c, |p| p == "/tmp/kitty-47").as_deref(),
            Some("unix:/tmp/kitty-47")
        );
        let c = ctx(&[("KITTY_PID", "5")], &[10]);
        assert_eq!(
            socket(&c, |p| p == "/tmp/kitty-5").as_deref(),
            Some("unix:/tmp/kitty-5")
        );
        assert_eq!(socket(&ctx(&[], &[10]), |_| false), None);
    }
}
