//! `agentlink ota <image.bin>`: asks the daemon to send a firmware image to the watch and shows its
//! progress lines. Closing this process (Ctrl-C) closes the socket, which stops the transfer.

use anyhow::Result;

#[cfg(unix)]
pub fn run(
    json: bool,
    image: &std::path::Path,
    unsigned: bool,
    device: Option<String>,
) -> Result<()> {
    use anyhow::{bail, Context};
    use std::io::{BufRead, BufReader, IsTerminal, Write};
    use std::os::unix::net::UnixStream;
    use std::time::Instant;

    let path = std::fs::canonicalize(image).with_context(|| format!("{}", image.display()))?;
    let sock = crate::home().join("hook.sock");
    let mut s = UnixStream::connect(&sock).with_context(|| {
        format!(
            "agentlinkd is not running (cannot connect to {})",
            sock.display()
        )
    })?;
    let req =
        serde_json::json!({"ctl": "ota", "path": path, "unsigned": unsigned, "device": device});
    writeln!(s, "{req}")?;

    let tty = std::io::stderr().is_terminal() && !json;
    let t0 = Instant::now();
    let (mut size, mut line_open) = (0u64, false);
    let mut next_decile = 0;
    let close_line = |open: &mut bool| {
        if *open {
            eprintln!();
            *open = false;
        }
    };
    for line in BufReader::new(s).lines() {
        let line = line.context("lost the connection to the daemon")?;
        let v: serde_json::Value = serde_json::from_str(&line)?;
        if json {
            println!("{v}");
        }
        match v["ota"].as_str().unwrap_or("") {
            "begin" => {
                size = v["size"].as_u64().unwrap_or(0);
                if !json {
                    eprintln!(
                        "sending {} ({} KB) to {}, replacing {}",
                        v["ver"].as_str().unwrap_or("?"),
                        size / 1024,
                        v["device"].as_str().unwrap_or("the watch"),
                        v["from"].as_str().unwrap_or("?")
                    );
                }
            }
            "progress" if !json => {
                let next = v["next"].as_u64().unwrap_or(0);
                let pct = next * 100 / size.max(1);
                let kbs = next as f64 / 1024.0 / t0.elapsed().as_secs_f64().max(0.001);
                if tty {
                    eprint!(
                        "\r  {pct:3}%  {} / {} KB  {kbs:.1} KB/s ",
                        next / 1024,
                        size / 1024
                    );
                    line_open = true;
                } else if pct / 10 >= next_decile {
                    eprintln!("  {pct}%");
                    next_decile = pct / 10 + 1;
                }
            }
            "done" if !json => {
                close_line(&mut line_open);
                eprintln!("sent and verified; the watch restarts");
            }
            "restarting" if !json => eprintln!("waiting for the watch to come back..."),
            "ok" => {
                if !json {
                    println!(
                        "updated: the watch now runs {}",
                        v["fw"].as_str().unwrap_or("?")
                    );
                }
                return Ok(());
            }
            "rolled_back" => {
                close_line(&mut line_open);
                bail!(
                    "the watch came back running {}: the new image was refused or rolled back",
                    v["fw"].as_str().unwrap_or("?")
                );
            }
            "error" => {
                close_line(&mut line_open);
                bail!("{}", v["msg"].as_str().unwrap_or("update failed"));
            }
            _ => {}
        }
    }
    bail!("the daemon closed the connection before the update ended")
}

#[cfg(not(unix))]
pub fn run(
    _json: bool,
    _image: &std::path::Path,
    _unsigned: bool,
    _device: Option<String>,
) -> Result<()> {
    anyhow::bail!("ota is not implemented on this platform yet")
}
