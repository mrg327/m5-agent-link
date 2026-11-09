//! `agentlink status`: one control request over the hook socket.

use anyhow::Result;

#[cfg(unix)]
pub fn run(json: bool) -> Result<()> {
    use anyhow::Context;
    use std::io::{BufRead, BufReader, Write};
    use std::os::unix::net::UnixStream;
    use std::time::Duration;

    let path = crate::home().join("hook.sock");
    let mut s = UnixStream::connect(&path).with_context(|| {
        format!(
            "agentlinkd is not running (cannot connect to {})",
            path.display()
        )
    })?;
    s.set_read_timeout(Some(Duration::from_secs(3)))?;
    s.write_all(b"{\"ctl\":\"status\"}\n")?;
    let mut line = String::new();
    BufReader::new(s)
        .read_line(&mut line)
        .context("no reply from daemon")?;
    let v: serde_json::Value = serde_json::from_str(&line)?;
    if json {
        println!("{v}");
        return Ok(());
    }
    println!(
        "agentlinkd {} (pid {}, up {}s)",
        v["version"].as_str().unwrap_or("?"),
        v["pid"],
        v["uptime_s"]
    );
    let devices = v["devices"].as_array().cloned().unwrap_or_default();
    println!("devices: {}", devices.len());
    for d in &devices {
        println!(
            "  {} {} {}",
            d["transport"].as_str().unwrap_or("?"),
            d["peer"].as_str().unwrap_or("?"),
            d["dev"]["board"].as_str().unwrap_or("")
        );
    }
    let sessions = v["sessions"].as_array().cloned().unwrap_or_default();
    println!("sessions: {}", sessions.len());
    for s in &sessions {
        println!(
            "  #{} {} {} {} {}{}",
            s["sid"],
            s["ag"].as_str().unwrap_or("?"),
            s["st"].as_str().unwrap_or("?"),
            s["repo"].as_str().unwrap_or(""),
            s["tool"].as_str().unwrap_or(""),
            match s["hn"].as_str() {
                Some(hn) if s["host"].as_u64().unwrap_or(0) != 0 => format!(" @{hn}"),
                _ => String::new(),
            }
        );
    }
    if let Some(remotes) = v["remotes"].as_array() {
        println!("remotes: {}", remotes.len());
        for r in remotes {
            println!(
                "  [{}] {} ({}) sessions: {}",
                r["idx"],
                r["name"].as_str().unwrap_or("?"),
                r["os"].as_str().unwrap_or("?"),
                r["sessions"]
            );
        }
    }
    if let Some(attaches) = v["attaches"].as_array() {
        println!("attached windows: {}", attaches.len());
        for a in attaches {
            println!(
                "  #{} {} window {}",
                a["id"],
                a["socket"].as_str().unwrap_or("?"),
                a["window"]
            );
        }
    }
    Ok(())
}

// TODO(windows): named pipe client.
#[cfg(not(unix))]
pub fn run(_json: bool) -> Result<()> {
    anyhow::bail!("status is not implemented on this platform yet")
}
