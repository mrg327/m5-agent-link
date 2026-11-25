//! `agentlink devices`: the watches agentlinkd serves over Bluetooth (PROTOCOL.md §8). The list is
//! `trusted-watches.txt` in the agentlink home, one device uid (hex) per line; the daemon reads it on
//! every connection (agentlinkd trust.rs), so a change applies when the watch next connects.

use anyhow::{bail, Result};

fn path() -> std::path::PathBuf {
    crate::home().join("trusted-watches.txt")
}

/// The file's uids, lower case (as agentlinkd trust.rs parses them).
fn parse(text: &str) -> Vec<String> {
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

fn check_uid(uid: &str) -> Result<String> {
    let u = uid.trim().to_ascii_lowercase();
    if u.is_empty() || u.len() > 32 || !u.chars().all(|c| c.is_ascii_hexdigit()) {
        bail!("not a device uid: {uid:?} (hex, as `agentlink status` shows it)");
    }
    Ok(u)
}

/// Lists the trusted watches.
pub fn list(json: bool) {
    let text = std::fs::read_to_string(path()).unwrap_or_default();
    let uids = parse(&text);
    if json {
        println!("{}", serde_json::json!({ "trusted": uids, "file": path() }));
    } else if uids.is_empty() {
        println!(
            "no trusted watches yet: the first watch over Bluetooth, or any watch on USB, is added"
        );
    } else {
        for line in text
            .lines()
            .filter(|l| !l.trim_start().starts_with('#') && !l.trim().is_empty())
        {
            println!("{}", line.trim());
        }
    }
}

/// Adds a watch to the list.
pub fn trust(uid: &str) -> Result<()> {
    use std::io::Write;
    let u = check_uid(uid)?;
    let p = path();
    let text = std::fs::read_to_string(&p).unwrap_or_default();
    if parse(&text).contains(&u) {
        println!("{u} is already trusted");
        return Ok(());
    }
    if let Some(dir) = p.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let mut f = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&p)?;
    if text.is_empty() {
        writeln!(
            f,
            "# Watches agentlinkd serves over Bluetooth (PROTOCOL.md §8); one uid per line."
        )?;
    }
    writeln!(
        f,
        "{u} # agentlink devices trust, {}",
        chrono::Local::now().format("%Y-%m-%d")
    )?;
    println!("{u} trusted: it is served when it next connects");
    Ok(())
}

/// Removes a watch from the list.
pub fn forget(uid: &str) -> Result<()> {
    let u = check_uid(uid)?;
    let p = path();
    let text = std::fs::read_to_string(&p).unwrap_or_default();
    if !parse(&text).contains(&u) {
        bail!("{u} is not in {}", p.display());
    }
    let kept: Vec<&str> = text
        .lines()
        .filter(|l| parse(l).first() != Some(&u))
        .collect();
    std::fs::write(&p, kept.join("\n") + "\n")?;
    println!("{u} forgotten: it is not served from its next connection (unpair it in the OS too)");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uids_are_hex_and_lower_case() {
        assert_eq!(check_uid(" 0102AbCd ").unwrap(), "0102abcd");
        check_uid("").unwrap_err();
        check_uid("zz").unwrap_err();
        assert_eq!(parse("# c\nAA # x\n\nbb\n"), vec!["aa", "bb"]);
    }
}
