//! Power reports from the watch (docs/design/POWER.md, proto 0.19): one CSV row per report in
//! `~/.agentlink/power-reports.csv`, which `agentlink power` analyses, and the last report of each device for
//! `agentlink status`. `~/.agentlink/power.csv` belongs to a separate logger and is never touched here.

use agentlink_proto::battery::{ocv_percent, POWER_CSV, POWER_CSV_COLUMNS};
use agentlink_proto::Power;
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

/// Who sent a report: the device as its hello named it, and the transport it came over.
pub struct Source<'a> {
    pub uid: &'a str,
    pub board: &'a str,
    pub fw: &'a str,
    pub transport: &'a str,
}

pub struct PowerLog {
    path: PathBuf,
    /// The last report per device uid, with when it came: for `agentlink status`.
    last: Mutex<BTreeMap<String, Value>>,
}

impl PowerLog {
    /// The log under the agentlink home (`AGENTLINK_HOME` or `~/.agentlink`).
    pub fn new() -> Self {
        Self::at(crate::paths::home().join(POWER_CSV))
    }

    pub fn at(path: PathBuf) -> Self {
        PowerLog {
            path,
            last: Mutex::default(),
        }
    }

    #[cfg(test)]
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Appends the report to the log (with the header first when the file is new or empty) and keeps it
    /// as the device's last. A write error is logged and otherwise ignored: the link goes on.
    pub fn record(&self, src: &Source, p: &Power, now_unix: u64) {
        let line = row(src, p, now_unix, &local_time(now_unix));
        if let Err(e) = append(&self.path, &line) {
            tracing::warn!("power report not logged to {}: {e}", self.path.display());
        }
        self.last.lock().unwrap().insert(
            src.uid.to_owned(),
            json!({
                "device": src.uid, "board": src.board, "fw": src.fw, "transport": src.transport,
                "at": now_unix, "ocv_pct": ocv_percent(p.mv as u32).map(round1), "report": p,
            }),
        );
    }

    /// The last report of every device that sent one since the daemon started, newest first.
    pub fn status(&self) -> Value {
        let mut v: Vec<Value> = self.last.lock().unwrap().values().cloned().collect();
        v.sort_by_key(|r| std::cmp::Reverse(r["at"].as_u64().unwrap_or(0)));
        Value::Array(v)
    }
}

impl Default for PowerLog {
    fn default() -> Self {
        Self::new()
    }
}

fn round1(x: f64) -> f64 {
    (x * 10.0).round() / 10.0
}

fn local_time(unix: u64) -> String {
    chrono::DateTime::from_timestamp(unix as i64, 0)
        .map(|t| {
            t.with_timezone(&chrono::Local)
                .to_rfc3339_opts(chrono::SecondsFormat::Secs, false)
        })
        .unwrap_or_default()
}

/// The CSV header: [`POWER_CSV_COLUMNS`].
pub fn header() -> String {
    POWER_CSV_COLUMNS.join(",")
}

/// One CSV row in [`POWER_CSV_COLUMNS`] order. Text fields lose commas, quotes and line breaks so the row
/// needs no quoting.
pub fn row(src: &Source, p: &Power, now_unix: u64, time: &str) -> String {
    let clean = |s: &str| -> String {
        s.chars()
            .map(|c| {
                if matches!(c, ',' | '"' | '\n' | '\r') {
                    ' '
                } else {
                    c
                }
            })
            .collect()
    };
    let b = |x: bool| if x { "1" } else { "0" };
    let ocv = ocv_percent(p.mv as u32)
        .map(|x| format!("{x:.1}"))
        .unwrap_or_default();
    let fields: Vec<String> = vec![
        now_unix.to_string(),
        clean(time),
        clean(src.uid),
        clean(src.board),
        clean(src.fw),
        clean(src.transport),
        p.seq.to_string(),
        p.mv.to_string(),
        p.mvf.to_string(),
        p.vin.to_string(),
        b(p.chg).into(),
        b(p.usb).into(),
        p.pct.to_string(),
        p.on.to_string(),
        p.dim.to_string(),
        p.off.to_string(),
        p.deep.to_string(),
        p.lk.to_string(),
        p.ble.to_string(),
        p.itvl.to_string(),
        p.nd.to_string(),
        ocv,
    ];
    debug_assert_eq!(fields.len(), POWER_CSV_COLUMNS.len());
    fields.join(",")
}

fn append(path: &Path, line: &str) -> std::io::Result<()> {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let mut f = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;
    let mut out = String::new();
    if f.metadata()?.len() == 0 {
        out.push_str(&header());
        out.push('\n');
    }
    out.push_str(line);
    out.push('\n');
    f.write_all(out.as_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    pub fn report(seq: u64, mv: u64) -> Power {
        Power {
            mv,
            mvf: mv + 9,
            vin: 0,
            chg: false,
            usb: false,
            pct: 63,
            on: 41200,
            dim: 30000,
            off: 228800,
            deep: 0,
            lk: 300000,
            ble: 300000,
            itvl: 30000,
            seq,
            nd: 0,
        }
    }

    const SRC: Source = Source {
        uid: "0102030405060708",
        board: "m5stopwatch",
        fw: "c040548,dirty",
        transport: "ble",
    };

    #[test]
    fn row_has_every_column_in_order() {
        let r = row(
            &SRC,
            &report(7, 3871),
            1_793_995_200,
            "2026-11-12T12:00:00-08:00",
        );
        let cols: Vec<&str> = r.split(',').collect();
        assert_eq!(cols.len(), POWER_CSV_COLUMNS.len());
        let get = |name: &str| cols[POWER_CSV_COLUMNS.iter().position(|c| *c == name).unwrap()];
        assert_eq!(get("time_unix"), "1793995200");
        assert_eq!(
            get("fw"),
            "c040548 dirty",
            "a comma in a field never splits the row"
        );
        assert_eq!(get("seq"), "7");
        assert_eq!(get("mv"), "3871");
        assert_eq!(get("chg"), "0");
        assert_eq!(get("off_ms"), "228800");
        assert_eq!(get("itvl_us"), "30000");
        assert_eq!(
            get("ocv_pct"),
            "60.1",
            "3871 mV: 60 % at 3870, 65 % at 3910"
        );
        let none = row(&SRC, &report(0, 0), 1, "");
        assert!(none.ends_with(','), "no reading, no estimate: {none}");
    }

    #[test]
    fn log_writes_the_header_once_and_keeps_the_last_report() {
        let dir = std::env::temp_dir().join(format!("al-power-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let log = PowerLog::at(dir.join(POWER_CSV));
        log.record(&SRC, &report(0, 3900), 100);
        log.record(&SRC, &report(1, 3890), 400);
        let other = Source {
            uid: "aa",
            board: "sim",
            fw: "0.1.0-sim",
            transport: "ws",
        };
        log.record(&other, &report(0, 4020), 300);
        let text = std::fs::read_to_string(log.path()).unwrap();
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines.len(), 4);
        assert_eq!(lines[0], header());
        assert_eq!(text.matches("time_unix").count(), 1, "one header");
        assert!(lines[2].starts_with("400,"));
        let st = log.status();
        assert_eq!(st.as_array().unwrap().len(), 2, "one entry per device");
        assert_eq!(st[0]["device"], "0102030405060708", "newest first");
        assert_eq!(st[0]["report"]["seq"], 1);
        assert_eq!(st[0]["report"]["mv"], 3890);
        assert!(!dir.join("power.csv").exists());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
