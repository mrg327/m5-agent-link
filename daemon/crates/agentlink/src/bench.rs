//! `agentlink bench run TASK...` plays task scripts on the watch through the daemon (BENCH.md) and writes a report
//! from the watch's frame timeline; `agentlink bench report FILE...` reads records the simulator saved.

use anyhow::{bail, Context, Result};
use serde_json::{json, Value};
use std::path::{Path, PathBuf};

/// One timeline record (BENCH.md §3).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Rec {
    pub t_us: u32,
    pub a: u32,
    pub b: u32,
    pub c: u32,
    pub kind: u8,
}

pub const FRAME: u8 = 0;
pub const DOWN: u8 = 1;
pub const UP: u8 = 2;
pub const GESTURE: u8 = 3;
pub const MARK: u8 = 4;
pub const CMD: u8 = 5;

/// gesture_kind_t values that are an input of their own (not a touch's): buttons, wrist lift, chord.
const OWN_INPUT_GESTURES: &[u32] = &[9, 10, 11, 12, 13, 19, 20];
/// Frames closer than this belong to one animation.
const ANIM_GAP_US: u32 = 100_000;
/// A gap inside an animation longer than one and a half LVGL periods (33 ms) is a slow frame.
const SLOW_GAP_US: u32 = 50_000;

pub fn parse_recs(b: &[u8]) -> Vec<Rec> {
    let le = |s: &[u8]| u32::from_le_bytes([s[0], s[1], s[2], s[3]]);
    b.chunks_exact(16)
        .map(|r| Rec {
            t_us: le(&r[0..4]),
            a: le(&r[4..8]),
            b: le(&r[8..12]),
            c: le(&r[12..16]) & 0xFF_FFFF,
            kind: r[15],
        })
        .collect()
}

/// What one run measured.
#[derive(Debug, Default, Clone, PartialEq)]
pub struct RunStats {
    /// First input → the last command the watch sent (ms); None without either.
    pub task_ms: Option<f64>,
    pub inputs: usize,
    pub touch_to_frame_ms: Vec<f64>,
    /// A played stroke's arrival to the touch read that played it (ms): how late the polled touch is read.
    pub read_late_ms: Vec<f64>,
    pub frame_ms: Vec<f64>,
    /// Start-to-start gaps between frames of one animation (ms).
    pub gaps_ms: Vec<f64>,
    pub slow_gaps: usize,
    pub frame_us: u64,
    pub wait_us: u64,
    pub px: u64,
}

pub fn analyse(recs: &[Rec]) -> RunStats {
    let start = recs.iter().find(|r| r.kind == MARK).map_or(0, |r| r.t_us);
    let recs: Vec<&Rec> = recs.iter().filter(|r| r.t_us >= start).collect();
    let is_input =
        |r: &Rec| r.kind == DOWN || (r.kind == GESTURE && OWN_INPUT_GESTURES.contains(&r.b));
    let first_input = recs.iter().find(|r| is_input(r)).map(|r| r.t_us);
    let last_cmd = recs.iter().rev().find(|r| r.kind == CMD).map(|r| r.t_us);
    let mut s = RunStats {
        task_ms: match (first_input, last_cmd) {
            (Some(a), Some(b)) if b >= a => Some((b - a) as f64 / 1000.0),
            _ => None,
        },
        inputs: recs.iter().filter(|r| is_input(r)).count(),
        ..Default::default()
    };
    let frames: Vec<&Rec> = recs.iter().copied().filter(|r| r.kind == FRAME).collect();
    s.read_late_ms = recs
        .iter()
        .filter(|r| r.kind == DOWN && r.a > 0)
        .map(|r| r.a as f64 / 1000.0)
        .collect();
    for r in recs.iter().filter(|r| r.kind == DOWN || r.kind == UP) {
        if let Some(f) = frames.iter().find(|f| f.t_us >= r.t_us) {
            s.touch_to_frame_ms
                .push((f.t_us + f.a - r.t_us) as f64 / 1000.0);
        }
    }
    // frames from the first input to the end of the run
    let from = first_input.unwrap_or(0);
    let task: Vec<&Rec> = frames.into_iter().filter(|f| f.t_us >= from).collect();
    for f in &task {
        s.frame_ms.push(f.a as f64 / 1000.0);
        s.frame_us += f.a as u64;
        s.wait_us += f.b as u64;
        s.px += f.c as u64;
    }
    for w in task.windows(2) {
        let gap = w[1].t_us - w[0].t_us;
        if gap <= ANIM_GAP_US {
            s.gaps_ms.push(gap as f64 / 1000.0);
            if gap > SLOW_GAP_US {
                s.slow_gaps += 1;
            }
        }
    }
    s
}

/// The p-th percentile (nearest rank) of `v`.
pub fn pct(v: &[f64], p: f64) -> Option<f64> {
    if v.is_empty() {
        return None;
    }
    let mut v = v.to_vec();
    v.sort_by(|a, b| a.total_cmp(b));
    let i = ((p / 100.0) * v.len() as f64).ceil() as usize;
    Some(v[i.clamp(1, v.len()) - 1])
}

/// One task over its runs.
#[derive(Debug, Clone)]
pub struct TaskStats {
    pub task: String,
    pub runs: Vec<RunStats>,
}

impl TaskStats {
    pub fn json(&self) -> Value {
        let all = |f: fn(&RunStats) -> &Vec<f64>| {
            self.runs
                .iter()
                .flat_map(|r| f(r).iter().copied())
                .collect::<Vec<_>>()
        };
        let times: Vec<f64> = self.runs.iter().filter_map(|r| r.task_ms).collect();
        let ttf = all(|r| &r.touch_to_frame_ms);
        let late = all(|r| &r.read_late_ms);
        let fr = all(|r| &r.frame_ms);
        let gaps = all(|r| &r.gaps_ms);
        let frame_us: u64 = self.runs.iter().map(|r| r.frame_us).sum();
        let wait_us: u64 = self.runs.iter().map(|r| r.wait_us).sum();
        let mut inputs: Vec<f64> = self.runs.iter().map(|r| r.inputs as f64).collect();
        inputs.sort_by(|a, b| a.total_cmp(b));
        json!({
            "task": self.task,
            "runs": self.runs.len(),
            "task_ms": {"median": pct(&times, 50.0), "worst": times.iter().copied().reduce(f64::max)},
            "inputs": pct(&inputs, 50.0),
            "touch_to_frame_ms": {"p50": pct(&ttf, 50.0), "p95": pct(&ttf, 95.0)},
            "read_late_ms": {"p50": pct(&late, 50.0), "p95": pct(&late, 95.0)},
            "frame_ms": {"p50": pct(&fr, 50.0), "p95": pct(&fr, 95.0), "max": fr.iter().copied().reduce(f64::max)},
            "frames": fr.len(),
            "slow_gaps": self.runs.iter().map(|r| r.slow_gaps).sum::<usize>(),
            "anim_fps": pct(&gaps, 50.0).map(|g| 1000.0 / g),
            "panel_wait": (frame_us > 0).then(|| wait_us as f64 / frame_us as f64),
            "px_per_frame": (!fr.is_empty()).then(|| self.runs.iter().map(|r| r.px).sum::<u64>() / fr.len() as u64),
        })
    }
}

fn ms(v: &Value) -> String {
    v.as_f64().map_or("–".into(), |x| {
        if x >= 100.0 {
            format!("{x:.0}")
        } else {
            format!("{x:.1}")
        }
    })
}

/// The report as Markdown: a header naming what the numbers depend on, then one row per task.
pub fn markdown(header: &str, tasks: &[Value]) -> String {
    let mut s = format!("# Bench report\n\n{header}\n\n");
    s.push_str("| Task | Runs | Time ms (median / worst) | Inputs | Touch read late ms (p50 / p95) | Touch→frame ms (p50 / p95) | Frame ms (p50 / p95 / max) | Slow gaps / frames | Anim fps | Panel wait |\n");
    s.push_str("|---|---|---|---|---|---|---|---|---|---|\n");
    for t in tasks {
        s.push_str(&format!(
            "| {} | {} | {} / {} | {} | {} / {} | {} / {} | {} / {} / {} | {} / {} | {} | {} |\n",
            t["task"].as_str().unwrap_or("?"),
            t["runs"],
            ms(&t["task_ms"]["median"]),
            ms(&t["task_ms"]["worst"]),
            t["inputs"]
                .as_f64()
                .map_or("–".into(), |x| format!("{x:.0}")),
            ms(&t["read_late_ms"]["p50"]),
            ms(&t["read_late_ms"]["p95"]),
            ms(&t["touch_to_frame_ms"]["p50"]),
            ms(&t["touch_to_frame_ms"]["p95"]),
            ms(&t["frame_ms"]["p50"]),
            ms(&t["frame_ms"]["p95"]),
            ms(&t["frame_ms"]["max"]),
            t["slow_gaps"],
            t["frames"],
            t["anim_fps"]
                .as_f64()
                .map_or("–".into(), |x| format!("{x:.0}")),
            t["panel_wait"]
                .as_f64()
                .map_or("–".into(), |x| format!("{:.0} %", x * 100.0)),
        ));
    }
    s.push_str(
        "\nTime: first input to the last command the watch sent. Touch read late: a played stroke's arrival to the polled touch read \
         that played it. Touch→frame: a touch-down or lift to the end of \
         the first frame drawn after it. Frame: render start to the end of the panel transfer. Slow gaps: frames \
         more than 50 ms apart inside one animation. Panel wait: the share of frame time spent waiting for the \
         panel. All times are the watch's own clock (BENCH.md).\n",
    );
    s
}

fn timeline_csv(recs: &[Rec]) -> String {
    let kinds = ["frame", "down", "up", "gesture", "mark", "cmd"];
    let mut s = String::from("t_us,kind,a,b,c\n");
    for r in recs {
        s.push_str(&format!(
            "{},{},{},{},{}\n",
            r.t_us,
            kinds.get(r.kind as usize).unwrap_or(&"?"),
            r.a,
            r.b,
            r.c
        ));
    }
    s
}

fn task_name(p: &Path) -> String {
    p.file_stem()
        .map_or("task".into(), |s| s.to_string_lossy().into_owned())
}

/// `agentlink bench report FILE...`: records the simulator saved (`bench save`), one file per run.
pub fn report(json_out: bool, files: &[PathBuf]) -> Result<()> {
    let mut tasks: Vec<TaskStats> = Vec::new();
    for f in files {
        let b = std::fs::read(f).with_context(|| format!("{}", f.display()))?;
        let run = analyse(&parse_recs(&b));
        let name = task_name(f);
        match tasks.iter_mut().find(|t| t.task == name) {
            Some(t) => t.runs.push(run),
            None => tasks.push(TaskStats {
                task: name,
                runs: vec![run],
            }),
        }
    }
    let rows: Vec<Value> = tasks.iter().map(TaskStats::json).collect();
    if json_out {
        println!("{}", json!({"source": "file", "tasks": rows}));
    } else {
        print!(
            "{}",
            markdown(
                "From saved records (simulator times are the desktop's, not the watch's).",
                &rows
            )
        );
    }
    Ok(())
}

/// What `run` needs besides the task files.
pub struct RunOpts {
    pub runs: u32,
    pub human: Option<u64>,
    pub out: Option<PathBuf>,
    pub device: Option<String>,
}

#[cfg(unix)]
fn one_run(
    task: &str,
    script: &str,
    human: Option<u64>,
    device: Option<&str>,
    json_out: bool,
) -> Result<Value> {
    use std::io::{BufRead, BufReader, Write};
    use std::os::unix::net::UnixStream;
    let sock = crate::home().join("hook.sock");
    let mut s = UnixStream::connect(&sock).with_context(|| {
        format!(
            "agentlinkd is not running (cannot connect to {})",
            sock.display()
        )
    })?;
    let req =
        json!({"ctl": "bench", "name": task, "script": script, "human": human, "device": device});
    writeln!(s, "{req}")?;
    for line in BufReader::new(s).lines() {
        let line = line.context("lost the connection to the daemon")?;
        let v: Value = serde_json::from_str(&line)?;
        if json_out {
            eprintln!("{v}");
        }
        match v["bench"].as_str().unwrap_or("") {
            "done" => return Ok(v),
            "error" => bail!("{task}: {}", v["msg"].as_str().unwrap_or("failed")),
            _ => {}
        }
    }
    bail!("{task}: the daemon closed the connection")
}

#[cfg(unix)]
pub fn run(json_out: bool, tasks: &[PathBuf], o: RunOpts) -> Result<()> {
    let stamp = chrono::Local::now().format("%Y%m%d-%H%M%S").to_string();
    let dir = o
        .out
        .clone()
        .unwrap_or_else(|| crate::home().join("bench").join(&stamp));
    std::fs::create_dir_all(&dir).with_context(|| format!("{}", dir.display()))?;
    let mut stats = Vec::new();
    let mut head: Option<Value> = None;
    for path in tasks {
        let script =
            std::fs::read_to_string(path).with_context(|| format!("{}", path.display()))?;
        let name = task_name(path);
        let mut t = TaskStats {
            task: name.clone(),
            runs: Vec::new(),
        };
        for i in 0..o.runs.max(1) {
            let seed = o.human.map(|s| s + i as u64);
            if !json_out {
                eprintln!("{name}: run {} of {}", i + 1, o.runs.max(1));
            }
            let v = one_run(&name, &script, seed, o.device.as_deref(), json_out)?;
            let recs = parse_recs(&hex::decode(v["recs"].as_str().unwrap_or(""))?);
            if v["lost"].as_u64().unwrap_or(0) > 0 {
                eprintln!(
                    "{name}: the watch's buffer filled; {} records lost",
                    v["lost"]
                );
            }
            std::fs::write(
                dir.join(format!("timeline-{name}-{}.csv", i + 1)),
                timeline_csv(&recs),
            )?;
            t.runs.push(analyse(&recs));
            head.get_or_insert(v);
        }
        stats.push(t);
    }
    let h = head.unwrap_or_default();
    let header = format!(
        "Watch {} running {}, over {}. agentlink {}, {}. User model: {}.",
        h["device"].as_str().unwrap_or("?"),
        h["fw"].as_str().unwrap_or("?"),
        h["transport"].as_str().unwrap_or("?"),
        env!("CARGO_PKG_VERSION"),
        stamp,
        o.human.map_or("exact input".into(), |s| format!(
            "seeded from {s} (σ 6 px, reaction median 250 ms)"
        )),
    );
    let rows: Vec<Value> = stats.iter().map(TaskStats::json).collect();
    let md = markdown(&header, &rows);
    std::fs::write(dir.join("report.md"), &md)?;
    let doc = json!({"device": h["device"], "fw": h["fw"], "transport": h["transport"], "human": o.human,
                     "runs": o.runs.max(1), "tasks": rows});
    std::fs::write(
        dir.join("report.json"),
        serde_json::to_string_pretty(&doc)? + "\n",
    )?;
    if json_out {
        println!("{doc}");
    } else {
        print!("{md}");
        eprintln!("\nwritten to {}", dir.display());
    }
    Ok(())
}

#[cfg(not(unix))]
pub fn run(_json: bool, _tasks: &[PathBuf], _o: RunOpts) -> Result<()> {
    bail!("bench is not implemented on this platform yet")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rec(t_us: u32, kind: u8, a: u32, b: u32, c: u32) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(&t_us.to_le_bytes());
        v.extend_from_slice(&a.to_le_bytes());
        v.extend_from_slice(&b.to_le_bytes());
        v.extend_from_slice(&(c | (kind as u32) << 24).to_le_bytes());
        v
    }

    #[test]
    fn records_parse_as_the_watch_packs_them() {
        let b = [
            rec(1000, FRAME, 8200, 3100, 21716),
            rec(5000, DOWN, 0, 0, 233 << 12 | 282),
        ]
        .concat();
        let r = parse_recs(&b);
        assert_eq!(
            r[0],
            Rec {
                t_us: 1000,
                a: 8200,
                b: 3100,
                c: 21716,
                kind: FRAME
            }
        );
        assert_eq!((r[1].kind, r[1].c >> 12, r[1].c & 0xFFF), (DOWN, 233, 282));
    }

    #[test]
    fn a_run_is_timed_from_the_first_input_to_the_last_command() {
        let b = [
            rec(0, FRAME, 9000, 1000, 100), // before the mark: not part of the task
            rec(10_000, MARK, 0, 1, 0),
            rec(20_000, DOWN, 0, 0, 0),
            rec(28_000, FRAME, 12_000, 4000, 2000), // the touch's answer, ending at 40 ms
            rec(61_000, FRAME, 10_000, 4000, 2000),
            rec(94_000, FRAME, 10_000, 4000, 2000),
            rec(160_000, FRAME, 10_000, 4000, 2000), // 66 ms after the last: a slow frame
            rec(1_620_000, UP, 0, 0, 0),
            rec(1_630_000, FRAME, 8_000, 4000, 1000),
            rec(1_640_000, GESTURE, 0, 4, 0),
            rec(1_650_000, CMD, 0, 3, 0),
        ]
        .concat();
        let s = analyse(&parse_recs(&b));
        assert_eq!(s.task_ms, Some(1630.0));
        assert_eq!(s.inputs, 1, "a touch's own gestures are not more inputs");
        assert_eq!(s.touch_to_frame_ms, vec![20.0, 18.0]);
        assert_eq!(s.frame_ms.len(), 5);
        assert_eq!(s.gaps_ms, vec![33.0, 33.0, 66.0]);
        assert_eq!(s.slow_gaps, 1);
        assert_eq!((s.frame_us, s.wait_us), (50_000, 20_000));
    }

    #[test]
    fn a_button_counts_as_an_input_and_percentiles_take_the_nearest_rank() {
        let b = [rec(0, GESTURE, 0, 9, 0), rec(500_000, CMD, 0, 0, 0)].concat();
        let s = analyse(&parse_recs(&b));
        assert_eq!((s.task_ms, s.inputs), (Some(500.0), 1));
        let v = [5.0, 1.0, 4.0, 2.0, 3.0];
        assert_eq!(
            (pct(&v, 50.0), pct(&v, 95.0), pct(&[], 50.0)),
            (Some(3.0), Some(5.0), None)
        );
    }

    #[test]
    fn the_report_has_a_row_per_task_and_dashes_for_what_was_not_measured() {
        let t = TaskStats {
            task: "approve".into(),
            runs: vec![RunStats::default()],
        };
        let md = markdown("header", &[t.json()]);
        assert!(md.contains("| approve | 1 | – / – | 0 | – / – |"), "{md}");
    }
}
