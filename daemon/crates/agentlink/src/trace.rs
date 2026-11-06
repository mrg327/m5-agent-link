//! `agentlink trace`: pretty-print (or pass through) the daemon's JSONL trace.

use anyhow::Result;
use serde_json::Value;
use std::io::{BufRead, BufReader, Seek, SeekFrom};
use std::path::PathBuf;
use std::time::Duration;

fn today_file() -> PathBuf {
    let day = chrono::Local::now().format("%Y-%m-%d");
    crate::home().join("trace").join(format!("{day}.jsonl"))
}

fn emit(line: &str, json: bool, sid: Option<u64>) {
    let Ok(v) = serde_json::from_str::<Value>(line) else {
        return println!("{line}");
    };
    if sid.is_some_and(|s| v["sid"].as_u64() != Some(s)) {
        return;
    }
    if json {
        return println!("{line}");
    }
    let ts = v["ts"]
        .as_str()
        .and_then(|t| t.get(11..23))
        .unwrap_or("??:??:??.???");
    let ids = [("sid", &v["sid"]), ("rid", &v["rid"])]
        .iter()
        .filter(|(_, x)| !x.is_null())
        .map(|(k, x)| format!(" {k}={x}"))
        .collect::<String>();
    let body = if v["msg"]["b"].is_null() {
        v["msg"].to_string()
    } else {
        v["msg"]["b"].to_string()
    };
    println!(
        "{ts} {:<8} {:<8} {}{ids} {body}",
        v["src"].as_str().unwrap_or("?"),
        v["dir"].as_str().unwrap_or("?"),
        v["kind"].as_str().unwrap_or("?")
    );
}

pub fn run(json: bool, follow: bool, sid: Option<u64>) -> Result<()> {
    let mut path = today_file();
    let mut offset = 0u64;
    loop {
        if let Ok(mut f) = std::fs::File::open(&path) {
            f.seek(SeekFrom::Start(offset))?;
            let mut r = BufReader::new(f);
            let mut line = String::new();
            // Only consume complete lines; a partial one is re-read next round.
            while r.read_line(&mut line)? > 0 {
                if !line.ends_with('\n') {
                    break;
                }
                offset += line.len() as u64;
                emit(line.trim_end(), json, sid);
                line.clear();
            }
        } else if !follow {
            anyhow::bail!("no trace file at {}", path.display());
        }
        if !follow {
            return Ok(());
        }
        std::thread::sleep(Duration::from_millis(200));
        let latest = today_file();
        if latest != path {
            path = latest;
            offset = 0;
        }
    }
}
