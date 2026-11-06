//! kitty: remote control selects (and on macOS raises) the OS window/tab/split.

use super::{args, env, generic, run, valid_kitty_id, valid_listen_on, FocusError, HostCtx};

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
