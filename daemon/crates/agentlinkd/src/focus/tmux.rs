//! tmux: select the agent's window and pane, then report the attached clients' ttys so the
//! terminal hosting them can be raised.

use super::{args, env, run, valid_tmux_pane, valid_tty, FocusError, HostCtx};

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
