//! Terminal.app: AppleScript finds the tab whose `tty` matches and raises its window.
//! The tty goes in as `argv`, never into the script text.

use super::{args, run, valid_tty, FocusError};

pub const SCRIPT: &str = r#"on run argv
    set targetTTY to item 1 of argv
    tell application "Terminal"
        repeat with w in windows
            repeat with t in tabs of w
                if tty of t is targetTTY then
                    if miniaturized of w then set miniaturized of w to false
                    set selected of t to true
                    set index of w to 1
                    activate
                    return "ok"
                end if
            end repeat
        end repeat
    end tell
    return "notfound"
end run"#;

/// `osascript` args: the script, then the values that reach it as `argv`.
pub fn osascript_args(tty: &str) -> Vec<String> {
    let mut a = args(["-e"]);
    a.push(SCRIPT.to_owned());
    a.push(tty.to_owned());
    a
}

pub fn focus(tty: Option<&str>) -> Result<(), FocusError> {
    let Some(tty) = tty.filter(|t| valid_tty(t)) else {
        return Err(FocusError::NoWindow);
    };
    let out = run("osascript", &osascript_args(tty))?;
    if !out.ok {
        return Err(if out.stderr.contains("-1743") {
            FocusError::Internal(
                "not allowed to control Terminal: enable agentlinkd (or the app that started \
                 it) under System Settings > Privacy & Security > Automation > Terminal"
                    .into(),
            )
        } else {
            FocusError::Internal(format!("osascript: {}", out.stderr.trim()))
        });
    }
    match out.stdout.trim() {
        "ok" => Ok(()),
        _ => Err(FocusError::NoWindow),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tty_is_passed_as_argv_not_interpolated() {
        let a = osascript_args("/dev/ttys003");
        assert_eq!(a.len(), 3);
        assert_eq!(a[0], "-e");
        assert!(a[1].starts_with("on run argv"));
        assert!(!a[1].contains("ttys003"));
        assert_eq!(a[2], "/dev/ttys003");
    }

    #[test]
    fn invalid_tty_never_reaches_osascript() {
        assert!(matches!(
            focus(Some("\" & (do shell script \"id\") & \"")),
            Err(FocusError::NoWindow)
        ));
        assert!(matches!(focus(None), Err(FocusError::NoWindow)));
    }
}
