//! Trusted watches (PROTOCOL.md §8). Any device can advertise an `AgentLink-` name, so a BLE link
//! is served only for a watch this PC trusts; anything else gets no welcome, no snapshot and no
//! commands. The list is `trusted-watches.txt` in the agentlink home: one device uid (hex) per
//! line, `#` starts a comment. `agentlink devices` shows and edits it.
//!
//! - USB: a watch on the cable is trusted, and recorded.
//! - WebSocket: the simulator on 127.0.0.1, trusted.
//! - BLE: trusted when listed. While the list is empty, the first BLE watch is recorded and trusted
//!   (so a watch that worked before this list existed keeps working); after that, a new watch needs
//!   a USB connection or `agentlink devices trust UID`.
//!
//! **Test only:** `AGENTLINK_TRUST_WS_AS_BLE` judges WebSocket links like BLE ones (device.rs).

use std::path::Path;

#[derive(Debug, PartialEq, Eq)]
pub enum Verdict {
    /// Listed, or on a transport that is trusted by nature.
    Trusted,
    /// Trusted and now recorded (`why` goes into the file).
    Added(&'static str),
    /// Not served.
    Refused,
}

/// The uids in the file's text, lower case.
pub fn parse(text: &str) -> Vec<String> {
    text.lines()
        .map(|l| {
            l.split('#')
                .next()
                .unwrap_or("")
                .trim()
                .to_ascii_lowercase()
        })
        .filter(|l| !l.is_empty())
        .collect()
}

/// What to do with device `uid` (hex) on `transport`, given the listed uids. Only BLE is judged:
/// USB records the watch, and every other transport (the 127.0.0.1 WebSocket, tests) is served.
pub fn decide(listed: &[String], uid: &str, transport: &str) -> Verdict {
    let uid = uid.to_ascii_lowercase();
    let known = listed.contains(&uid);
    match transport {
        _ if known => Verdict::Trusted,
        "usb" => Verdict::Added("usb"),
        "ble" if listed.is_empty() => Verdict::Added("first ble watch"),
        "ble" => Verdict::Refused,
        _ => Verdict::Trusted,
    }
}

/// Decides for `uid` with the list in `path`, and records it when the verdict adds it. A list that
/// cannot be read counts as empty; a record that cannot be written still trusts this link.
pub fn check(path: &Path, uid: &str, transport: &str) -> Verdict {
    let listed = std::fs::read_to_string(path)
        .map(|t| parse(&t))
        .unwrap_or_default();
    let v = decide(&listed, uid, transport);
    if let Verdict::Added(why) = v {
        if let Err(e) = append(path, uid, why) {
            tracing::warn!("trusted watches: cannot record {uid}: {e}");
        }
    }
    v
}

fn append(path: &Path, uid: &str, why: &str) -> std::io::Result<()> {
    use std::io::Write;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let new = !path.exists();
    let mut f = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;
    if new {
        writeln!(
            f,
            "# Watches agentlinkd serves over Bluetooth (PROTOCOL.md §8); one uid per line."
        )?;
    }
    let date = chrono::Local::now().format("%Y-%m-%d");
    writeln!(f, "{} # {why}, {date}", uid.to_ascii_lowercase())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn l(v: &[&str]) -> Vec<String> {
        v.iter().map(ToString::to_string).collect()
    }

    #[test]
    fn parse_skips_comments_and_case() {
        let t = "# header\n0102AABB # usb, 2026-11-25\n\n  ccdd  \n";
        assert_eq!(parse(t), l(&["0102aabb", "ccdd"]));
    }

    #[test]
    fn transports_and_the_first_ble_watch() {
        assert_eq!(decide(&[], "aa", "ws"), Verdict::Trusted);
        assert_eq!(decide(&l(&["bb"]), "aa", "test"), Verdict::Trusted);
        assert_eq!(decide(&[], "aa", "usb"), Verdict::Added("usb"));
        assert_eq!(decide(&[], "aa", "ble"), Verdict::Added("first ble watch"));
        let one = l(&["aa"]);
        assert_eq!(decide(&one, "AA", "ble"), Verdict::Trusted);
        assert_eq!(decide(&one, "bb", "ble"), Verdict::Refused);
        assert_eq!(decide(&one, "bb", "usb"), Verdict::Added("usb"));
    }

    #[test]
    fn check_records_and_then_refuses_a_stranger() {
        let dir = std::env::temp_dir().join(format!("al-trust-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let path = dir.join("trusted-watches.txt");
        assert_eq!(
            check(&path, "AA01", "ble"),
            Verdict::Added("first ble watch")
        );
        assert_eq!(check(&path, "aa01", "ble"), Verdict::Trusted);
        assert_eq!(check(&path, "bb02", "ble"), Verdict::Refused);
        assert_eq!(check(&path, "bb02", "usb"), Verdict::Added("usb"));
        assert_eq!(check(&path, "bb02", "ble"), Verdict::Trusted);
        let text = std::fs::read_to_string(&path).unwrap();
        assert_eq!(parse(&text), l(&["aa01", "bb02"]));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
