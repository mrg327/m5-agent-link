//! kitty: remote control selects (and on macOS raises) the OS window/tab/split.

use super::{args, env, generic, run, valid_kitty_id, valid_listen_on, FocusError, HostCtx};

const MAC_BIN: &str = "/Applications/kitty.app/Contents/MacOS/kitty";

const NEEDS_REMOTE_CONTROL: &str = "kitty remote control is not enabled: add \
    `allow_remote_control socket-only` and `listen_on unix:/tmp/kitty` to kitty.conf and \
    restart kitty (only the app was raised)";

pub fn focus_args(listen_on: &str, id: &str) -> Vec<String> {
    let mut a = args(["@", "--to"]);
    a.push(listen_on.to_owned());
    a.extend(args(["focus-window", "--match"]));
    a.push(format!("id:{id}"));
    a
}

pub fn focus(ctx: &HostCtx, os: &str) -> Result<(), FocusError> {
    let id = env(ctx, "KITTY_WINDOW_ID").filter(|i| valid_kitty_id(i));
    let listen = env(ctx, "KITTY_LISTEN_ON").filter(|l| valid_listen_on(l));
    let (Some(id), Some(listen)) = (id, listen) else {
        let _ = raise(ctx, os);
        return Err(FocusError::Internal(NEEDS_REMOTE_CONTROL.into()));
    };
    let a = focus_args(listen, id);
    let out = match run("kitty", &a) {
        Err(FocusError::Unsupported) => run(MAC_BIN, &a)?,
        r => r?,
    };
    if !out.ok {
        let _ = raise(ctx, os);
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
            focus_args("unix:/tmp/kitty-1", "7"),
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
}
