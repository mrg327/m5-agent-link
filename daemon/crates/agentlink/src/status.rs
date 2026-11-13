//! `agentlink status`: one control request over the hook socket.

use anyhow::Result;

/// One control request (`{"ctl": ...}`) over the hook socket, and the daemon's one-line reply.
#[cfg(unix)]
pub(crate) fn ctl(req: &serde_json::Value) -> Result<serde_json::Value> {
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
    s.write_all(format!("{req}\n").as_bytes())?;
    let mut line = String::new();
    BufReader::new(s)
        .read_line(&mut line)
        .context("no reply from daemon")?;
    Ok(serde_json::from_str(&line)?)
}

// TODO(windows): named pipe client.
#[cfg(not(unix))]
pub(crate) fn ctl(_req: &serde_json::Value) -> Result<serde_json::Value> {
    anyhow::bail!("control requests are not implemented on this platform yet")
}

pub fn run(json: bool) -> Result<()> {
    let v = ctl(&serde_json::json!({"ctl": "status"}))?;
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
    print_presence(&v["presence"]);
    print_keep_awake(&v["keep_awake"]);
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
    print_power(&v["power"]);
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

/// What the daemon believes about the user (docs/design/PRESENCE.md), so the user can see why the
/// watch did or did not buzz.
fn print_presence(p: &serde_json::Value) {
    if p.is_null() {
        return;
    }
    let state = p["state"].as_str().unwrap_or("unknown");
    let how = match (p["enabled"].as_bool(), p["source"].as_str()) {
        (Some(false), _) => " (presence rules are off: alerts as usual)".to_string(),
        (_, Some(src)) if src != "none" => {
            let idle = p["idle_s"]
                .as_u64()
                .map(|s| format!(", idle {s}s of {}s", p["idle_limit_s"]))
                .unwrap_or_default();
            format!(" (via {src}{idle})")
        }
        _ => " (no source works here: treated as away)".to_string(),
    };
    println!("presence: {state}{how}");
    for s in p["sessions"].as_array().into_iter().flatten() {
        let f = s["focus"].as_str().unwrap_or("unknown");
        if f != "unknown" {
            println!(
                "  #{} terminal focused: {f} (via {})",
                s["sid"],
                s["source"].as_str().unwrap_or("?")
            );
        }
    }
}

/// The last power report of each device (docs/design/POWER.md); `agentlink power` has the analysis.
fn print_power(p: &serde_json::Value) {
    let Some(list) = p.as_array().filter(|l| !l.is_empty()) else {
        return;
    };
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    println!("power (last report):");
    for d in list {
        let r = &d["report"];
        let ms = |k: &str| r[k].as_u64().unwrap_or(0);
        let mins = |k: &str| format!("{:.1}", ms(k) as f64 / 60000.0);
        let ocv = d["ocv_pct"]
            .as_f64()
            .map(|x| format!(" ({x:.0} % by the LiPo curve)"))
            .unwrap_or_default();
        let supply = match (r["usb"].as_bool(), r["chg"].as_bool()) {
            (_, Some(true)) => "charging",
            (Some(true), _) => "on USB",
            _ => "on battery",
        };
        let itvl = match ms("itvl") {
            0 => "no BLE".to_string(),
            us => format!("BLE interval {:.1} ms", us as f64 / 1000.0),
        };
        println!(
            "  {} {}: {} mV{ocv}, {supply}, {itvl}, {}s ago (seq {})",
            d["board"].as_str().unwrap_or("?"),
            d["device"].as_str().unwrap_or("?"),
            ms("mv"),
            now.saturating_sub(d["at"].as_u64().unwrap_or(now)),
            ms("seq")
        );
        println!(
            "    since the report before: on {} min, dim {} min, off {} min, deep {} min ({} deep sleeps)",
            mins("on"),
            mins("dim"),
            mins("off"),
            mins("deep"),
            ms("nd")
        );
    }
}

/// The sleep inhibitor (docs/design/PRESENCE.md section 5).
fn print_keep_awake(k: &serde_json::Value) {
    if k.is_null() {
        return;
    }
    let power = match k["on_ac"].as_bool() {
        Some(true) => "on AC power",
        Some(false) => "on battery",
        None => "power source unknown",
    };
    let detail = k["detail"]
        .as_str()
        .map(|d| format!(": {d}"))
        .unwrap_or_default();
    println!(
        "keep awake: {} (setting {}, {}, {}{}{})",
        k["state"].as_str().unwrap_or("?"),
        k["mode"].as_str().unwrap_or("?"),
        if k["busy"].as_bool() == Some(true) {
            "work running"
        } else {
            "nothing running"
        },
        power,
        k["how"]
            .as_str()
            .map(|h| format!(", via {h}"))
            .unwrap_or_default(),
        detail
    );
}
