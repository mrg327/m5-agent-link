//! Fallbacks: activate a macOS app by name, or raise an X11 window with xdotool/wmctrl.

use super::{args, env, run, valid_kitty_id, FocusError, HostCtx};

pub fn activate_app(name: &'static str) -> Result<(), FocusError> {
    let out = run("open", &args(["-a", name]))?;
    if out.ok {
        Ok(())
    } else {
        Err(FocusError::NoWindow)
    }
}

/// X11: the window id from `$WINDOWID` if present, else the window owned by one of the
/// captured pids (or kitty's pid). Wayland has no portable equivalent.
pub fn linux(ctx: &HostCtx) -> Result<(), FocusError> {
    let mut id = env(ctx, "WINDOWID")
        .filter(|w| valid_kitty_id(w))
        .map(str::to_owned);
    if id.is_none() {
        let kitty = env(ctx, "KITTY_PID").and_then(|p| p.parse::<u32>().ok());
        for pid in kitty.into_iter().chain(ctx.pids.iter().copied()) {
            let out = run(
                "xdotool",
                &args(["search", "--onlyvisible", "--pid", &pid.to_string()]),
            )?;
            if let Some(w) = out
                .stdout
                .lines()
                .map(str::trim)
                .find(|w| valid_kitty_id(w))
            {
                id = Some(w.to_owned());
                break;
            }
        }
    }
    let id = id.ok_or(FocusError::NoWindow)?;
    match run("xdotool", &args(["windowactivate", &id])) {
        Err(FocusError::Unsupported) => {
            let hex = format!(
                "{:#x}",
                id.parse::<u64>().map_err(|_| FocusError::NoWindow)?
            );
            let out = run("wmctrl", &args(["-i", "-a", &hex]))?;
            if out.ok {
                Ok(())
            } else {
                Err(FocusError::NoWindow)
            }
        }
        Ok(o) if o.ok => Ok(()),
        Ok(_) => Err(FocusError::NoWindow),
        Err(e) => Err(e),
    }
}
