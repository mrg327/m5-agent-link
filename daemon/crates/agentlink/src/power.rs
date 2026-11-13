//! `agentlink power`: where the battery goes. Reads the daemon's report log
//! (`~/.agentlink/power-reports.csv`, docs/design/POWER.md) and fits the state of charge, taken from the
//! battery voltage, against the time the watch spent in each power state. Read-only.

use agentlink_proto::battery::{ocv_percent, POWER_CSV};
use anyhow::{bail, Context, Result};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::PathBuf;

/// The power states, in the order of the log's `*_ms` columns.
const STATES: [&str; 4] = ["on", "dim", "off", "deep"];
/// A state needs this much time in the window before its drain rate is fitted.
const MIN_STATE_MS: f64 = 10.0 * 60_000.0;
/// The fit needs the charge to have moved at least this far (percentage points).
const MIN_SPREAD_PTS: f64 = 1.5;
/// ... and at least this many residual degrees of freedom.
const MIN_DOF: i64 = 3;
const MS_PER_H: f64 = 3_600_000.0;

/// Command-line options.
pub struct Opts {
    pub csv: Option<PathBuf>,
    pub device: Option<String>,
    pub hours: Option<f64>,
    pub all: bool,
    pub capacity_mah: f64,
}

/// One report: the time and voltage at its end and the device time spent in each state before it.
#[derive(Debug, Clone, Default)]
pub struct Row {
    pub time_unix: i64,
    pub device: String,
    pub board: String,
    pub fw: String,
    pub seq: i64,
    pub mv: u32,
    pub mvf: u32,
    pub chg: bool,
    pub usb: bool,
    pub pct: f64,
    pub itvl_us: u32,
    /// on, dim, off, deep (ms).
    pub ms: [f64; 4],
}

impl Row {
    fn on_battery(&self) -> bool {
        !self.usb && !self.chg
    }
    /// The voltage to use: the raw reading, else the filtered one.
    fn volts(&self) -> Option<f64> {
        match (self.mv, self.mvf) {
            (0, 0) => None,
            (0, f) => Some(f64::from(f)),
            (m, _) => Some(f64::from(m)),
        }
    }
    fn soc(&self) -> Option<f64> {
        ocv_percent(self.mv).or_else(|| ocv_percent(self.mvf))
    }
}

/// Parse the log by header name. Unknown columns are ignored, order is free, only the first ten
/// (time_unix .. deep_ms) are required. Returns the rows and how many lines were skipped as malformed.
pub fn parse_csv(text: &str) -> Result<(Vec<Row>, usize)> {
    let mut lines = text.lines().filter(|l| !l.trim().is_empty());
    let Some(header) = lines.next() else {
        return Ok((Vec::new(), 0));
    };
    let cols: HashMap<&str, usize> = header
        .split(',')
        .enumerate()
        .map(|(i, c)| (c.trim(), i))
        .collect();
    for need in [
        "time_unix",
        "device",
        "seq",
        "mv",
        "chg",
        "usb",
        "on_ms",
        "dim_ms",
        "off_ms",
        "deep_ms",
    ] {
        if !cols.contains_key(need) {
            bail!("the report log has no `{need}` column in its header");
        }
    }
    let (mut rows, mut skipped) = (Vec::new(), 0);
    for line in lines {
        let f: Vec<&str> = line.split(',').map(str::trim).collect();
        match parse_row(&cols, &f) {
            Some(r) => rows.push(r),
            None => skipped += 1,
        }
    }
    Ok((rows, skipped))
}

fn parse_row(cols: &HashMap<&str, usize>, f: &[&str]) -> Option<Row> {
    let get = |name: &str| cols.get(name).and_then(|&i| f.get(i).copied());
    let req = |name: &str| get(name).filter(|s| !s.is_empty());
    let num = |name: &str| req(name)?.parse::<f64>().ok().filter(|v| v.is_finite());
    let flag = |name: &str| req(name).map(|s| s != "0");
    let opt = |name: &str| num(name).unwrap_or(0.0);
    let ms = |name: &str| num(name).filter(|&v| v >= 0.0);
    let device = req("device")?.to_string();
    Some(Row {
        time_unix: num("time_unix")? as i64,
        device,
        board: get("board").unwrap_or("").to_string(),
        fw: get("fw").unwrap_or("").to_string(),
        seq: num("seq")? as i64,
        mv: num("mv")? as u32,
        mvf: opt("mvf") as u32,
        chg: flag("chg")?,
        usb: flag("usb")?,
        pct: opt("pct"),
        itvl_us: opt("itvl_us") as u32,
        ms: [ms("on_ms")?, ms("dim_ms")?, ms("off_ms")?, ms("deep_ms")?],
    })
}

/// The battery as of the device's last report.
#[derive(Debug, Clone)]
pub struct Current {
    pub mv: u32,
    pub mvf: u32,
    pub ocv_pct: Option<f64>,
    pub pct: f64,
    pub chg: bool,
    pub usb: bool,
    pub age_s: i64,
    pub itvl_us: u32,
    pub seq: i64,
    pub board: String,
    pub fw: String,
    pub time_unix: i64,
}

/// Which reports the analysis used.
#[derive(Debug, Clone)]
pub struct Window {
    /// since_unplug | hours | all | last_battery_run
    pub kind: &'static str,
    pub start_unix: Option<i64>,
    pub end_unix: Option<i64>,
    /// Device time covered by the fitted intervals (the sum of the segment spans).
    pub hours: f64,
    pub reports: usize,
    pub segments: usize,
}

/// The overall discharge rate.
#[derive(Debug, Clone)]
pub struct Overall {
    pub pct_per_h: f64,
    pub mv_per_h: f64,
    pub ma: f64,
    pub soc_start: f64,
    pub soc_end: f64,
}

/// One power state's share of the window and its fitted drain.
#[derive(Debug, Clone)]
pub struct StateRow {
    pub state: &'static str,
    pub ms: f64,
    pub share: f64,
    pub rate: Option<f64>,
    pub se: Option<f64>,
    pub ma: Option<f64>,
    pub drain_pct: Option<f64>,
    pub drain_share: Option<f64>,
    /// fitted | zero | too_little_time | not_resolved | no_fit
    pub status: &'static str,
}

/// Whether the per-state fit was possible, and how well it did.
#[derive(Debug, Clone)]
pub struct FitInfo {
    pub ok: bool,
    pub reason: Option<String>,
    pub intervals: usize,
    pub segments: usize,
    pub rms_pct: Option<f64>,
}

#[derive(Debug, Clone)]
pub struct Analysis {
    pub device: String,
    pub current: Current,
    pub window: Window,
    pub ext_now: bool,
    pub overall: Option<Overall>,
    pub states: Vec<StateRow>,
    pub fit: FitInfo,
    pub skipped_rows: usize,
    pub capacity_mah: f64,
}

/// A voltage reading with the cumulative device time since its segment's first report.
struct Point {
    soc: f64,
    mv: f64,
    t: f64,
    ts: [f64; 4],
}

/// Pick the device, the window and the rows in it, and run the analysis.
pub fn analyze(rows: &[Row], opts: &Opts, now_unix: i64) -> Result<Analysis> {
    let Some(last_any) = rows.last() else {
        bail!("no power reports yet");
    };
    let device = match &opts.device {
        Some(p) => {
            let p = p.to_ascii_lowercase();
            match rows
                .iter()
                .rev()
                .find(|r| r.device.to_ascii_lowercase().starts_with(&p))
            {
                Some(r) => r.device.clone(),
                None => bail!("no reports from a device starting with `{p}`"),
            }
        }
        None => rows
            .iter()
            .rev()
            .find(|r| r.board != "sim")
            .unwrap_or(last_any)
            .device
            .clone(),
    };
    let dev: Vec<&Row> = rows.iter().filter(|r| r.device == device).collect();
    let last = *dev.last().expect("device chosen from rows");
    let ext_now = !last.on_battery();

    let (kind, win): (&'static str, Vec<&Row>) = if opts.all {
        (
            "all",
            dev.iter().copied().filter(|r| r.on_battery()).collect(),
        )
    } else if let Some(h) = opts.hours {
        let cutoff = last.time_unix as f64 - h * 3600.0;
        (
            "hours",
            dev.iter()
                .copied()
                .filter(|r| r.on_battery() && r.time_unix as f64 >= cutoff)
                .collect(),
        )
    } else if !ext_now {
        let from = dev
            .iter()
            .rposition(|r| !r.on_battery())
            .map_or(0, |i| i + 1);
        ("since_unplug", dev[from..].to_vec())
    } else {
        // on external power now: the most recent run of battery reports
        match dev.iter().rposition(|r| r.on_battery()) {
            None => ("last_battery_run", Vec::new()),
            Some(end) => {
                let from = dev[..end]
                    .iter()
                    .rposition(|r| !r.on_battery())
                    .map_or(0, |i| i + 1);
                ("last_battery_run", dev[from..=end].to_vec())
            }
        }
    };

    let current = Current {
        mv: last.mv,
        mvf: last.mvf,
        ocv_pct: last.soc(),
        pct: last.pct,
        chg: last.chg,
        usb: last.usb,
        age_s: now_unix - last.time_unix,
        itvl_us: last.itvl_us,
        seq: last.seq,
        board: last.board.clone(),
        fw: last.fw.clone(),
        time_unix: last.time_unix,
    };

    // Rows without a usable voltage are dropped; the seq check below then splits around them.
    let usable: Vec<&Row> = win
        .iter()
        .copied()
        .filter(|r| r.soc().is_some() && r.volts().is_some())
        .collect();
    let segs = segments(&usable);
    let n_segs = segs.len();
    let n_points: usize = segs.iter().map(|s| s.len()).sum();
    let intervals = n_points - n_segs;
    let span: f64 = segs.iter().map(|s| s.last().map_or(0.0, |p| p.t)).sum();
    let mut state_h = [0.0f64; 4];
    for s in &segs {
        if let Some(p) = s.last() {
            for (a, b) in state_h.iter_mut().zip(p.ts) {
                *a += b;
            }
        }
    }
    let total_h: f64 = state_h.iter().sum();

    let window = Window {
        kind,
        start_unix: usable.first().map(|r| r.time_unix),
        end_unix: usable.last().map(|r| r.time_unix),
        hours: span,
        reports: usable.len(),
        segments: n_segs,
    };

    let overall = overall_rate(&segs, intervals, opts.capacity_mah);

    // The fit: SoC_k = a_seg - sum_s r_s T_s(k), intercepts removed by centering each segment.
    let eligible: Vec<usize> = (0..4)
        .filter(|&s| state_h[s] * MS_PER_H >= MIN_STATE_MS)
        .collect();
    let (lo, hi) = segs
        .iter()
        .flatten()
        .fold((f64::MAX, f64::MIN), |(l, h), p| {
            (l.min(p.soc), h.max(p.soc))
        });
    let spread = if hi >= lo { hi - lo } else { 0.0 };
    let mut fit = FitInfo {
        ok: false,
        reason: None,
        intervals,
        segments: n_segs,
        rms_pct: None,
    };
    let mut result: Option<Fit> = None;
    if usable.is_empty() {
        fit.reason = Some("no battery reports in the window".into());
    } else if eligible.is_empty() {
        fit.reason = Some("no power state has 10 minutes in the window".into());
    } else if spread < MIN_SPREAD_PTS {
        fit.reason = Some(format!(
            "the charge moved only {spread:.1} points in the window (need {MIN_SPREAD_PTS})"
        ));
    } else {
        match fit_states(&segs, &eligible) {
            Some(f) if f.dof >= MIN_DOF => {
                fit.ok = true;
                fit.rms_pct = Some((f.rss / n_points as f64).sqrt());
                result = Some(f);
            }
            Some(f) => {
                fit.reason = Some(format!(
                    "only {intervals} intervals in {n_segs} segment(s) for a fit with {} state(s) \
                     (need {MIN_DOF} more than the fit uses)",
                    f.subset.len()
                ));
            }
            None => fit.reason = Some("the fit is degenerate".into()),
        }
    }

    let total_drain: f64 = result.as_ref().map_or(0.0, |f| {
        f.subset
            .iter()
            .zip(&f.rates)
            .map(|(&s, &r)| r * state_h[s])
            .sum()
    });
    let states = (0..4)
        .map(|s| {
            let mut row = StateRow {
                state: STATES[s],
                ms: state_h[s] * MS_PER_H,
                share: if total_h > 0.0 {
                    state_h[s] / total_h
                } else {
                    0.0
                },
                rate: None,
                se: None,
                ma: None,
                drain_pct: None,
                drain_share: None,
                status: if eligible.contains(&s) {
                    "no_fit"
                } else {
                    "too_little_time"
                },
            };
            if let (Some(f), true) = (&result, eligible.contains(&s)) {
                let pos = f.subset.iter().position(|&x| x == s);
                let rate = pos.map_or(0.0, |i| f.rates[i]);
                let se = pos.and_then(|i| f.se.get(i).copied().flatten());
                let drain = rate * state_h[s];
                row.rate = Some(rate);
                row.se = se;
                row.ma = Some(rate * opts.capacity_mah / 100.0);
                row.drain_pct = Some(drain);
                row.drain_share = (total_drain > 0.0).then(|| drain / total_drain);
                row.status = if pos.is_none() {
                    "zero"
                } else if se.is_some_and(|e| e > rate) {
                    "not_resolved"
                } else {
                    "fitted"
                };
            }
            row
        })
        .collect();

    Ok(Analysis {
        device,
        current,
        window,
        ext_now,
        overall,
        states,
        fit,
        skipped_rows: 0,
        capacity_mah: opts.capacity_mah,
    })
}

/// Split into runs of consecutive reports (seq = previous + 1) and build each run's points: the first
/// report is time zero (its own interval predates the first voltage), later ones add their intervals.
fn segments(rows: &[&Row]) -> Vec<Vec<Point>> {
    let mut segs: Vec<Vec<Point>> = Vec::new();
    let mut prev_seq = None;
    for r in rows {
        let (Some(soc), Some(mv)) = (r.soc(), r.volts()) else {
            continue;
        };
        let cont = prev_seq == Some(r.seq - 1) && !segs.is_empty();
        prev_seq = Some(r.seq);
        let point = if cont {
            let p = segs.last().and_then(|s| s.last()).expect("continuing");
            let mut ts = p.ts;
            for (a, ms) in ts.iter_mut().zip(r.ms) {
                *a += ms / MS_PER_H;
            }
            Point {
                soc,
                mv,
                t: ts.iter().sum(),
                ts,
            }
        } else {
            segs.push(Vec::new());
            Point {
                soc,
                mv,
                t: 0.0,
                ts: [0.0; 4],
            }
        };
        segs.last_mut().expect("pushed").push(point);
    }
    segs
}

/// Pooled within-segment slope of SoC and of mV against device hours.
fn overall_rate(segs: &[Vec<Point>], intervals: usize, capacity_mah: f64) -> Option<Overall> {
    if intervals < 3 {
        return None;
    }
    let (mut sxx, mut sxy, mut sxv) = (0.0, 0.0, 0.0);
    for s in segs {
        let n = s.len() as f64;
        let (mt, ms, mv) = (
            s.iter().map(|p| p.t).sum::<f64>() / n,
            s.iter().map(|p| p.soc).sum::<f64>() / n,
            s.iter().map(|p| p.mv).sum::<f64>() / n,
        );
        for p in s {
            sxx += (p.t - mt) * (p.t - mt);
            sxy += (p.t - mt) * (p.soc - ms);
            sxv += (p.t - mt) * (p.mv - mv);
        }
    }
    if sxx < 1e-12 {
        return None;
    }
    let rate = -sxy / sxx;
    Some(Overall {
        pct_per_h: rate,
        mv_per_h: -sxv / sxx,
        ma: rate * capacity_mah / 100.0,
        soc_start: segs.first()?.first()?.soc,
        soc_end: segs.last()?.last()?.soc,
    })
}

/// The chosen non-negative least-squares solution.
struct Fit {
    /// State indices with a fitted (possibly zero-bounded) rate; the others are fixed at 0.
    subset: Vec<usize>,
    rates: Vec<f64>,
    se: Vec<Option<f64>>,
    rss: f64,
    dof: i64,
}

/// Non-negative least squares by enumeration: ordinary least squares on every subset of the eligible
/// states, keeping the feasible one (all rates >= 0) with the smallest residual sum of squares.
fn fit_states(segs: &[Vec<Point>], eligible: &[usize]) -> Option<Fit> {
    // Centered data: z = -(SoC - segment mean), x = T_s - segment mean, so z = sum r_s x_s.
    let mut data: Vec<([f64; 4], f64)> = Vec::new();
    for s in segs {
        let n = s.len() as f64;
        let mean_soc = s.iter().map(|p| p.soc).sum::<f64>() / n;
        let mut mean_t = [0.0; 4];
        for p in s {
            for (m, t) in mean_t.iter_mut().zip(p.ts) {
                *m += t / n;
            }
        }
        for p in s {
            let mut x = [0.0; 4];
            for k in 0..4 {
                x[k] = p.ts[k] - mean_t[k];
            }
            data.push((x, mean_soc - p.soc));
        }
    }
    let dof_base = data.len() as i64 - segs.len() as i64;
    let rss0: f64 = data.iter().map(|d| d.1 * d.1).sum();
    let mut best = Fit {
        subset: Vec::new(),
        rates: Vec::new(),
        se: Vec::new(),
        rss: rss0,
        dof: dof_base,
    };
    let m = eligible.len();
    let mut masks: Vec<u32> = (1..1u32 << m).collect();
    masks.sort_by_key(|k| k.count_ones()); // smaller subsets win ties
    for mask in masks {
        let sub: Vec<usize> = (0..m)
            .filter(|i| mask >> i & 1 == 1)
            .map(|i| eligible[i])
            .collect();
        let p = sub.len();
        let mut a = vec![vec![0.0; p]; p];
        let mut b = vec![0.0; p];
        for (x, z) in &data {
            for i in 0..p {
                b[i] += x[sub[i]] * z;
                for j in 0..p {
                    a[i][j] += x[sub[i]] * x[sub[j]];
                }
            }
        }
        let Some(inv) = invert(&a) else { continue };
        let r: Vec<f64> = (0..p)
            .map(|i| (0..p).map(|j| inv[i][j] * b[j]).sum())
            .collect();
        if r.iter().any(|&v| v < -1e-9) {
            continue;
        }
        let r: Vec<f64> = r.iter().map(|&v| v.max(0.0)).collect();
        let rss: f64 = data
            .iter()
            .map(|(x, z)| {
                let e = z - (0..p).map(|i| r[i] * x[sub[i]]).sum::<f64>();
                e * e
            })
            .sum();
        if rss < best.rss * (1.0 - 1e-12) {
            let dof = dof_base - p as i64;
            let se = (0..p)
                .map(|i| (dof > 0).then(|| (rss / dof as f64 * inv[i][i]).max(0.0).sqrt()))
                .collect();
            best = Fit {
                subset: sub,
                rates: r,
                se,
                rss,
                dof,
            };
        }
    }
    if best.subset.is_empty() {
        // nothing helped: all rates zero; the dof is that of the empty model
        best.dof = dof_base;
    }
    Some(best)
}

/// Gauss-Jordan inverse with partial pivoting; `None` when (nearly) singular.
fn invert(a: &[Vec<f64>]) -> Option<Vec<Vec<f64>>> {
    let n = a.len();
    let scale = a
        .iter()
        .flatten()
        .fold(0.0f64, |m, v| m.max(v.abs()))
        .max(1e-300);
    let mut m: Vec<Vec<f64>> = (0..n)
        .map(|i| {
            let mut row = a[i].clone();
            row.extend((0..n).map(|j| if i == j { 1.0 } else { 0.0 }));
            row
        })
        .collect();
    for c in 0..n {
        let piv = (c..n).max_by(|&x, &y| m[x][c].abs().total_cmp(&m[y][c].abs()))?;
        if m[piv][c].abs() < 1e-10 * scale {
            return None;
        }
        m.swap(c, piv);
        let d = m[c][c];
        for v in &mut m[c] {
            *v /= d;
        }
        for r in 0..n {
            if r != c {
                let f = m[r][c];
                if f != 0.0 {
                    let pivot_row = m[c].clone();
                    for (x, p) in m[r].iter_mut().zip(pivot_row) {
                        *x -= f * p;
                    }
                }
            }
        }
    }
    Some(m.into_iter().map(|r| r[n..].to_vec()).collect())
}

// ---- output ----

/// "1 h 05 m" from hours.
fn fmt_hm(hours: f64) -> String {
    let mins = (hours * 60.0).round().max(0.0) as i64;
    format!("{} h {:02} m", mins / 60, mins % 60)
}

fn fmt_age(s: i64) -> String {
    match s {
        s if s < 0 => "in the future (clock skew?)".into(),
        s if s < 90 => format!("{s} s ago"),
        s if s < 90 * 60 => format!("{} min ago", (s + 30) / 60),
        s => format!("{} ago", fmt_hm(s as f64 / 3600.0)),
    }
}

fn fmt_local(unix: Option<i64>) -> String {
    unix.and_then(|t| chrono::DateTime::from_timestamp(t, 0))
        .map(|d| {
            d.with_timezone(&chrono::Local)
                .format("%Y-%m-%d %H:%M")
                .to_string()
        })
        .unwrap_or_else(|| "?".into())
}

/// Left-align the columns of `rows` with two spaces between them (the last column is not padded).
fn table(rows: &[Vec<String>]) -> Vec<String> {
    let cols = rows.iter().map(Vec::len).max().unwrap_or(0);
    let widths: Vec<usize> = (0..cols)
        .map(|c| {
            rows.iter()
                .map(|r| r.get(c).map_or(0, |s| s.chars().count()))
                .max()
                .unwrap_or(0)
        })
        .collect();
    rows.iter()
        .map(|r| {
            let mut line = String::new();
            for (c, cell) in r.iter().enumerate() {
                if c + 1 == r.len() {
                    line.push_str(cell);
                } else {
                    line.push_str(&format!("{cell:<w$}  ", w = widths[c]));
                }
            }
            line.trim_end().to_string()
        })
        .collect()
}

/// The human-readable report.
pub fn render_text(a: &Analysis) -> String {
    const PAD: &str = "          ";
    let mut out = String::new();
    let c = &a.current;
    let power = match (c.chg, c.usb) {
        (true, _) => "charging",
        (false, true) => "on USB power, not charging",
        _ => "on battery",
    };
    let raw = if c.mv == 0 {
        "no raw reading".to_string()
    } else {
        format!("raw {} mV", c.mv)
    };
    let ocv = c.ocv_pct.map_or("?".to_string(), |p| format!("{p:.0}"));
    let volts = f64::from(if c.mv > 0 { c.mv } else { c.mvf }) / 1000.0;
    out += &format!(
        "battery   {volts:.2} V ({raw}, filtered {} mV): {ocv} % by the LiPo curve (the watch shows {:.0} %), {power}\n",
        c.mvf, c.pct
    );
    let ble = if c.itvl_us > 0 {
        format!("BLE interval {} ms", c.itvl_us / 1000)
    } else {
        "no BLE".into()
    };
    out += &format!(
        "{PAD}last report {} (seq {}), {ble}; {} {}, device {}\n",
        fmt_age(c.age_s),
        c.seq,
        if c.board.is_empty() { "?" } else { &c.board },
        c.fw,
        a.device
    );

    let w = &a.window;
    let from = fmt_local(w.start_unix);
    let what = match w.kind {
        "since_unplug" => format!("since the last unplug, {from}"),
        "hours" => format!("battery reports from {from}"),
        "all" => format!("all battery reports, from {from}"),
        _ => format!("last run on battery, from {from} (on external power now)"),
    };
    out += &format!(
        "window    {what}: {} in {} report{}, {} segment{}\n",
        fmt_hm(w.hours),
        w.reports,
        if w.reports == 1 { "" } else { "s" },
        w.segments,
        if w.segments == 1 { "" } else { "s" },
    );
    if w.reports == 0 {
        out += &format!("{PAD}no battery reports to analyse\n");
        return out;
    }

    match &a.overall {
        Some(o) => {
            out += &format!(
                "overall   {:.1} %/h ({:.0} mV/h), about {:.0} mA at {:.0} mAh; {:.0} % -> {:.0} %\n",
                o.pct_per_h, o.mv_per_h, o.ma, a.capacity_mah, o.soc_start, o.soc_end
            );
        }
        None => out += "overall   not enough data for a rate yet (needs 3 intervals)\n",
    }

    if !a.fit.ok {
        out += &format!(
            "{PAD}not enough data to split the drain by state yet: {}\n",
            a.fit.reason.as_deref().unwrap_or("?")
        );
    }
    let mut rows = vec![["", "time", "share", "rate", "about", "drain"]
        .map(String::from)
        .to_vec()];
    for s in &a.states {
        let share = format!("{:.0} %", s.share * 100.0);
        let time = fmt_hm(s.ms / MS_PER_H);
        let mut r = vec![s.state.to_string(), time, share];
        match (s.status, s.rate) {
            ("too_little_time", _) => r.push("too little time".into()),
            (_, Some(rate)) => {
                r.push(match (s.status, s.se) {
                    ("zero", _) => "fit says ~0".into(),
                    (st, Some(se)) => format!(
                        "{rate:.1} ±{se:.1} %/h{}",
                        if st == "not_resolved" {
                            " (not resolved)"
                        } else {
                            ""
                        }
                    ),
                    (_, None) => format!("{rate:.1} %/h"),
                });
                r.push(format!("{:.0} mA", s.ma.unwrap_or(0.0)));
                r.push(format!(
                    "{:.1} pts ({:.0} %)",
                    s.drain_pct.unwrap_or(0.0),
                    s.drain_share.unwrap_or(0.0) * 100.0
                ));
            }
            _ => {}
        }
        rows.push(r);
    }
    for (i, line) in table(&rows).iter().enumerate() {
        out += &format!("{}{line}\n", if i == 0 { "state     " } else { PAD });
    }
    if a.fit.ok {
        out += &format!(
            "fit       {} intervals in {} segment{}, rms {:.1} pts\n",
            a.fit.intervals,
            a.fit.segments,
            if a.fit.segments == 1 { "" } else { "s" },
            a.fit.rms_pct.unwrap_or(0.0)
        );
    }
    out += "caveat    estimates come from voltage only (sag under load, temperature, a generic LiPo curve); see docs/design/POWER.md\n";
    if a.skipped_rows > 0 {
        out += &format!(
            "{PAD}{} malformed row(s) in the log skipped\n",
            a.skipped_rows
        );
    }
    out
}

/// The same report in machine form.
pub fn to_json(a: &Analysis) -> Value {
    let c = &a.current;
    let w = &a.window;
    json!({
        "device": a.device,
        "capacity_mah": a.capacity_mah,
        "external_power_now": a.ext_now,
        "current": {
            "mv": c.mv, "mvf": c.mvf, "ocv_pct": c.ocv_pct, "pct": c.pct, "chg": c.chg, "usb": c.usb,
            "age_s": c.age_s, "itvl_us": c.itvl_us, "seq": c.seq, "board": c.board, "fw": c.fw,
            "time_unix": c.time_unix,
        },
        "window": {
            "kind": w.kind, "start_unix": w.start_unix, "end_unix": w.end_unix, "hours": w.hours,
            "reports": w.reports, "segments": w.segments,
        },
        "overall": a.overall.as_ref().map(|o| json!({
            "pct_per_h": o.pct_per_h, "mv_per_h": o.mv_per_h, "ma": o.ma,
            "soc_start": o.soc_start, "soc_end": o.soc_end,
        })),
        "states": a.states.iter().map(|s| json!({
            "state": s.state, "ms": s.ms, "share": s.share, "rate_pct_per_h": s.rate, "se": s.se,
            "ma": s.ma, "drain_pct": s.drain_pct, "drain_share": s.drain_share, "status": s.status,
        })).collect::<Vec<_>>(),
        "fit": {
            "ok": a.fit.ok, "reason": a.fit.reason, "intervals": a.fit.intervals,
            "segments": a.fit.segments, "rms_pct": a.fit.rms_pct,
        },
        "skipped_rows": a.skipped_rows,
    })
}

/// `agentlink power`: read the log, analyse, print.
pub fn run(json: bool, opts: Opts) -> Result<()> {
    let path = opts
        .csv
        .clone()
        .unwrap_or_else(|| crate::home().join(POWER_CSV));
    let none = || {
        format!(
            "no power reports yet: {} (watch firmware with proto 0.19, linked to agentlinkd)",
            path.display()
        )
    };
    let text = std::fs::read_to_string(&path).map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            anyhow::anyhow!(none())
        } else {
            anyhow::Error::new(e).context(format!("reading {}", path.display()))
        }
    })?;
    let (rows, skipped) = parse_csv(&text).with_context(|| path.display().to_string())?;
    if rows.is_empty() {
        bail!(none());
    }
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs() as i64);
    let mut a = analyze(&rows, &opts, now)?;
    a.skipped_rows = skipped;
    if json {
        println!("{}", to_json(&a));
    } else {
        print!("{}", render_text(&a));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use agentlink_proto::battery::{OCV_CURVE, POWER_CSV_COLUMNS};

    const RATES: [f64; 4] = [40.0, 20.0, 2.0, 0.3];
    const NOW: i64 = 1_800_000_000;

    struct Lcg(u64);
    impl Lcg {
        fn next(&mut self) -> f64 {
            self.0 = self
                .0
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            (self.0 >> 11) as f64 / (1u64 << 53) as f64
        }
    }

    /// Inverse of the OCV curve, in whole mV.
    fn soc_to_mv(p: f64) -> u32 {
        let p = p.clamp(0.0, 100.0);
        let i = OCV_CURVE
            .iter()
            .position(|&(_, s)| s >= p)
            .unwrap_or(OCV_CURVE.len() - 1)
            .max(1);
        let ((v0, p0), (v1, p1)) = (OCV_CURVE[i - 1], OCV_CURVE[i]);
        (f64::from(v0) + f64::from(v1 - v0) * (p - p0) / (p1 - p0)).round() as u32
    }

    fn row(time: i64, seq: i64, mv: u32, ext: bool, ms: [f64; 4]) -> Row {
        Row {
            time_unix: time,
            device: "0102030405060708".into(),
            board: "m5stopwatch".into(),
            fw: "1a2b3c4".into(),
            seq,
            mv,
            mvf: mv,
            chg: ext,
            usb: ext,
            pct: 50.0,
            itvl_us: 30_000,
            ms,
        }
    }

    /// `n` battery reports, 5 min apart, seq from `seq0`, drained at RATES with a varied state mix
    /// (and the odd long deep-sleep interval) and +-3 mV of noise. Returns the rows and the true
    /// end charge. The first report's own interval is not drained (it predates the first voltage).
    fn battery_run(rng: &mut Lcg, n: usize, seq0: i64, t0: i64, soc0: f64) -> (Vec<Row>, f64) {
        let mut soc = soc0;
        let mut rows = Vec::new();
        for k in 0..n {
            let mut ms = [0.0; 4];
            let mut span = 300_000.0;
            if k % 17 == 16 {
                span = 1_800_000.0;
                ms[3] = span;
            } else {
                let on = if rng.next() < 0.3 {
                    rng.next() * 0.2
                } else {
                    0.0
                };
                let dim = if rng.next() < 0.5 {
                    rng.next() * 0.2
                } else {
                    0.0
                };
                let off = rng.next() * 0.5;
                ms = [
                    on * span,
                    dim * span,
                    off * span,
                    (1.0 - on - dim - off) * span,
                ];
            }
            if k > 0 {
                soc -= (0..4).map(|s| RATES[s] * ms[s] / MS_PER_H).sum::<f64>();
            }
            let noise = (rng.next() * 7.0).floor() as i64 - 3;
            let mv = (i64::from(soc_to_mv(soc)) + noise) as u32;
            rows.push(row(t0 + 300 * k as i64, seq0 + k as i64, mv, false, ms));
        }
        (rows, soc)
    }

    fn opts() -> Opts {
        Opts {
            csv: None,
            device: None,
            hours: None,
            all: false,
            capacity_mah: 450.0,
        }
    }

    fn state<'a>(a: &'a Analysis, name: &str) -> &'a StateRow {
        a.states.iter().find(|s| s.state == name).unwrap()
    }

    fn big_run() -> (Vec<Row>, f64) {
        battery_run(&mut Lcg(7), 240, 0, NOW - 240 * 300, 95.0)
    }

    #[test]
    fn fit_recovers_state_rates() {
        let (rows, _) = big_run();
        let a = analyze(&rows, &opts(), NOW).unwrap();
        assert!(a.fit.ok, "{:?}", a.fit.reason);
        assert_eq!(a.window.segments, 1);
        assert_eq!(a.fit.intervals, 239);
        for (name, truth, tol) in [("on", 40.0, 6.0), ("dim", 20.0, 4.0), ("off", 2.0, 1.0)] {
            let s = state(&a, name);
            let r = s.rate.unwrap();
            assert!(
                (r - truth).abs() < tol,
                "{name}: {r} vs {truth} (se {:?})",
                s.se
            );
            assert!(s.se.is_some() && s.ma.is_some());
        }
        let deep = state(&a, "deep").rate.unwrap();
        assert!(deep < 1.0, "deep {deep}");
        let shares: f64 = a.states.iter().map(|s| s.share).sum();
        assert!((shares - 1.0).abs() < 1e-9);
        assert!(a.fit.rms_pct.unwrap() < 1.5);
        let drain: f64 = a.states.iter().filter_map(|s| s.drain_share).sum();
        assert!((drain - 1.0).abs() < 1e-9);
    }

    #[test]
    fn overall_rate_matches_truth() {
        let (rows, end) = big_run();
        let a = analyze(&rows, &opts(), NOW).unwrap();
        let o = a.overall.unwrap();
        let hours: f64 = rows[1..]
            .iter()
            .map(|r| r.ms.iter().sum::<f64>())
            .sum::<f64>()
            / MS_PER_H;
        assert!((a.window.hours - hours).abs() < 1e-9);
        let truth = (95.0 - end) / hours;
        assert!(
            (o.pct_per_h - truth).abs() < 0.15 * truth,
            "{} vs {truth}",
            o.pct_per_h
        );
        assert!((o.ma - o.pct_per_h * 4.5).abs() < 1e-9);
        assert!(o.mv_per_h > 0.0);
        assert!((o.soc_start - 95.0).abs() < 2.0 && (o.soc_end - end).abs() < 2.0);
    }

    #[test]
    fn window_since_unplug_uses_trailing_battery_run() {
        let mut rows: Vec<Row> = (0..10)
            .map(|k| {
                row(
                    NOW - 10_000 + 300 * k,
                    k,
                    4000,
                    k % 2 == 0,
                    [0.0, 0.0, 0.0, 300_000.0],
                )
            })
            .collect();
        rows.push(row(NOW - 7000, 10, 4100, true, [0.0; 4]));
        let (run, _) = battery_run(&mut Lcg(3), 30, 11, NOW - 6000, 90.0);
        rows.extend(run);
        let a = analyze(&rows, &opts(), NOW).unwrap();
        assert_eq!(a.window.kind, "since_unplug");
        assert_eq!(a.window.reports, 30);
        assert!(!a.ext_now);
        assert_eq!(a.window.start_unix, Some(NOW - 6000));
        // --all also takes the battery rows from before (the five odd-numbered of the first ten)
        let all = analyze(
            &rows,
            &Opts {
                all: true,
                ..opts()
            },
            NOW,
        )
        .unwrap();
        assert_eq!(all.window.kind, "all");
        assert_eq!(all.window.reports, 35);
    }

    #[test]
    fn window_on_external_power_uses_last_battery_run() {
        let (mut rows, _) = battery_run(&mut Lcg(3), 20, 0, NOW - 9000, 90.0);
        rows.push(row(NOW - 3000, 20, 4000, true, [0.0; 4]));
        rows.push(row(NOW - 2700, 21, 4010, true, [0.0; 4]));
        let a = analyze(&rows, &opts(), NOW).unwrap();
        assert!(a.ext_now);
        assert_eq!(a.window.kind, "last_battery_run");
        assert_eq!(a.window.reports, 20);
        assert!(a.current.usb && a.current.chg);
        assert_eq!(a.current.seq, 21);
        assert_eq!(a.current.age_s, 2700);
        assert!(render_text(&a).contains("external power"));
    }

    #[test]
    fn window_hours_and_all() {
        let (mut a_rows, _) = battery_run(&mut Lcg(5), 20, 0, NOW - 20_000, 95.0);
        a_rows.push(row(NOW - 14_000, 20, 4150, true, [0.0; 4]));
        let (b_rows, _) = battery_run(&mut Lcg(6), 40, 21, NOW - 13_000, 95.0);
        let last = b_rows.last().unwrap().time_unix;
        a_rows.extend(b_rows);
        let h = analyze(
            &a_rows,
            &Opts {
                hours: Some(1.0),
                ..opts()
            },
            NOW,
        )
        .unwrap();
        assert_eq!(h.window.kind, "hours");
        assert_eq!(h.window.reports, 13); // 3600 s / 300 s + 1
        assert!(a_rows.iter().filter(|r| r.time_unix >= last - 3600).count() >= 13);
        let all = analyze(
            &a_rows,
            &Opts {
                all: true,
                ..opts()
            },
            NOW,
        )
        .unwrap();
        assert_eq!(all.window.reports, 60);
        assert_eq!(all.window.segments, 2); // the USB row breaks the seq run
    }

    #[test]
    fn seq_gap_and_reset_split_segments() {
        let mut rng = Lcg(9);
        let (mut rows, end) = battery_run(&mut rng, 40, 0, NOW - 30_000, 90.0);
        // lost reports: seq jumps by 3
        let (b, end2) = battery_run(&mut rng, 40, 43, NOW - 18_000, end);
        rows.extend(b);
        // power-on reset: seq starts again at 0
        let (c, _) = battery_run(&mut rng, 40, 0, NOW - 6_000, end2);
        rows.extend(c);
        let a = analyze(&rows, &opts(), NOW).unwrap();
        assert_eq!(a.window.segments, 3);
        assert_eq!(a.window.reports, 120);
        assert_eq!(a.fit.intervals, 120 - 3);
        assert!(a.fit.ok, "{:?}", a.fit.reason);
        // each segment's first report is excluded: 3 x 39 intervals of 5 min (or long deep ones)
        let o = a.overall.unwrap();
        assert!(o.pct_per_h > 1.0 && o.pct_per_h < 10.0, "{}", o.pct_per_h);
    }

    #[test]
    fn short_window_is_not_enough_data() {
        let (rows, _) = battery_run(&mut Lcg(1), 4, 0, NOW - 1200, 90.0);
        let a = analyze(&rows, &opts(), NOW).unwrap();
        assert!(!a.fit.ok);
        assert!(a.fit.reason.is_some());
        assert!(a.states.iter().all(|s| s.rate.is_none()));
        let t = render_text(&a);
        assert!(
            t.contains("not enough data to split the drain by state yet:"),
            "{t}"
        );
        assert!(t.contains("share") || t.contains("state"));
        // only a hair of discharge: the spread reason
        let flat: Vec<Row> = (0..30)
            .map(|k| {
                row(
                    NOW - 9000 + 300 * k,
                    k,
                    3800,
                    false,
                    [0.0, 0.0, 300_000.0, 0.0],
                )
            })
            .collect();
        let a = analyze(&flat, &opts(), NOW).unwrap();
        assert!(!a.fit.ok);
        assert!(a.fit.reason.unwrap().contains("moved only"));
        assert!(a.overall.is_some());
    }

    #[test]
    fn state_under_ten_minutes_is_too_little_time() {
        // only off and deep sleep for a while, plus five minutes of "on"
        let mut rng = Lcg(2);
        let mut soc = 90.0;
        let mut rows = Vec::new();
        for k in 0..100i64 {
            let ms = if k == 50 {
                [300_000.0, 0.0, 0.0, 0.0]
            } else if k % 2 == 0 {
                [0.0, 0.0, 300_000.0, 0.0]
            } else {
                [0.0, 0.0, 0.0, 300_000.0]
            };
            if k > 0 {
                soc -= (0..4).map(|s| RATES[s] * ms[s] / MS_PER_H).sum::<f64>();
            }
            let mv = soc_to_mv(soc) + (rng.next() * 3.0) as u32;
            rows.push(row(NOW - 30_000 + 300 * k, k, mv, false, ms));
        }
        let a = analyze(&rows, &opts(), NOW).unwrap();
        assert_eq!(state(&a, "on").status, "too_little_time");
        assert_eq!(state(&a, "dim").status, "too_little_time");
        assert!(state(&a, "on").rate.is_none());
        assert!(render_text(&a).contains("too little time"));
        let j = to_json(&a);
        assert_eq!(j["states"][0]["status"], "too_little_time");
    }

    #[test]
    fn parse_by_header_name_with_reordering_and_extras() {
        let text = "extra,deep_ms,off_ms,dim_ms,on_ms,usb,chg,mv,seq,device,time_unix,board\n\
                    x,4,3,2,1,0,1,3900,5,abcd,1700000000,sim\n\
                    y,40,30,20,10,1,0,0,6,abcd,1700000300,m5stopwatch\n";
        let (rows, skipped) = parse_csv(text).unwrap();
        assert_eq!(skipped, 0);
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].ms, [1.0, 2.0, 3.0, 4.0]);
        assert!(rows[0].chg && !rows[0].usb);
        assert_eq!((rows[0].mv, rows[0].mvf, rows[0].seq), (3900, 0, 5));
        assert_eq!(rows[0].board, "sim");
        assert!(rows[1].usb && !rows[1].chg);
        assert_eq!(rows[1].soc(), None); // no voltage at all
        assert!(parse_csv("time_unix,device,seq\n1,a,1\n").is_err());
        assert_eq!(parse_csv("").unwrap().0.len(), 0);
    }

    #[test]
    fn parse_skips_and_counts_malformed_rows() {
        let h = POWER_CSV_COLUMNS.join(",");
        let good = |seq: i64| {
            format!(
                "1700000{seq:03},2026-01-01T00:00:00+00:00,dev1,m5stopwatch,abc,ble,{seq},3900,3900,0,0,0,60,1000,2000,3000,4000,0,0,0,0,65.0"
            )
        };
        let text = format!(
            "{h}\n{}\nnot,a,row\n{}\n1700000009,t,dev1,b,f,ble,x,3900,3900,0,0,0,60,1,2,3,4,0,0,0,0,65\n\n{h}\n{}\n",
            good(1),
            good(2),
            good(3)
        );
        let (rows, skipped) = parse_csv(&text).unwrap();
        assert_eq!(rows.len(), 3);
        assert_eq!(skipped, 3); // short row, bad seq, repeated header
        assert_eq!(rows[2].seq, 3);
        assert_eq!(rows[0].ms, [1000.0, 2000.0, 3000.0, 4000.0]);
    }

    #[test]
    fn default_device_skips_sim_rows() {
        let mut real = row(NOW - 600, 3, 3900, false, [0.0; 4]);
        real.device = "real".into();
        let mut sim = row(NOW - 300, 9, 3900, false, [0.0; 4]);
        sim.device = "simdev".into();
        sim.board = "sim".into();
        let rows = vec![real.clone(), sim.clone()];
        assert_eq!(analyze(&rows, &opts(), NOW).unwrap().device, "real");
        assert_eq!(
            analyze(
                &rows,
                &Opts {
                    device: Some("SIM".into()),
                    ..opts()
                },
                NOW
            )
            .unwrap()
            .device,
            "simdev"
        );
        assert!(analyze(
            &rows,
            &Opts {
                device: Some("zz".into()),
                ..opts()
            },
            NOW
        )
        .is_err());
        assert_eq!(analyze(&[sim], &opts(), NOW).unwrap().device, "simdev");
        assert!(analyze(&[], &opts(), NOW).is_err());
    }

    #[test]
    fn json_has_the_documented_shape() {
        let (rows, _) = big_run();
        let mut a = analyze(&rows, &opts(), NOW).unwrap();
        a.skipped_rows = 2;
        let j = to_json(&a);
        assert_eq!(j["device"], "0102030405060708");
        assert_eq!(j["current"]["itvl_us"], 30_000);
        assert_eq!(j["window"]["kind"], "since_unplug");
        assert_eq!(j["window"]["segments"], 1);
        assert!(j["overall"]["pct_per_h"].as_f64().unwrap() > 0.0);
        assert_eq!(j["states"].as_array().unwrap().len(), 4);
        assert_eq!(j["states"][0]["state"], "on");
        assert!(["fitted", "not_resolved"].contains(&j["states"][0]["status"].as_str().unwrap()));
        assert_eq!(j["fit"]["ok"], true);
        assert_eq!(j["skipped_rows"], 2);
        let t = render_text(&a);
        for want in [
            "battery",
            "window",
            "overall",
            "state",
            "fit",
            "caveat",
            "2 malformed",
        ] {
            assert!(t.contains(want), "{want} in\n{t}");
        }
    }

    #[test]
    fn invert_handles_singular_and_regular() {
        assert!(invert(&[vec![1.0, 2.0], vec![2.0, 4.0]]).is_none());
        let i = invert(&[vec![2.0, 0.0], vec![1.0, 4.0]]).unwrap();
        assert!((i[0][0] - 0.5).abs() < 1e-12 && (i[1][0] + 0.125).abs() < 1e-12);
    }

    /// Writes a synthetic log for trying the command by hand:
    /// `cargo test -p agentlink write_sample_csv -- --ignored`, then `agentlink power --csv <path>`.
    #[test]
    #[ignore]
    fn write_sample_csv() {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as i64;
        let (rows, _) = battery_run(&mut Lcg(7), 240, 0, now - 120 - 239 * 300, 95.0);
        let mut out = POWER_CSV_COLUMNS.join(",") + "\n";
        for r in rows {
            let t = chrono::DateTime::from_timestamp(r.time_unix, 0)
                .unwrap()
                .to_rfc3339();
            out += &format!(
                "{},{t},{},{},{},ble,{},{},{},0,0,0,{:.0},{:.0},{:.0},{:.0},{:.0},0,0,{},0,\n",
                r.time_unix,
                r.device,
                r.board,
                r.fw,
                r.seq,
                r.mv,
                r.mvf,
                r.pct,
                r.ms[0],
                r.ms[1],
                r.ms[2],
                r.ms[3],
                r.itvl_us
            );
        }
        let dir =
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/power-sample");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("power-reports.csv"), out).unwrap();
    }
}
