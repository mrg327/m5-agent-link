//! Jobs (docs/design/JOBS.md): commands typed into a terminal that are still running after
//! `AGENTLINK_JOB_MIN_S` seconds get a session like an agent's.
//!
//! The shell's integration (`agentlink shell-init`) reports `start` / `end` / `exit` through
//! `agentlink-hook shell …`. Every start and end is tracked, also for ignored and short commands:
//! `rerun` must know whether the shell is busy. A job reaches the registry (and so the devices)
//! only once it has run past the threshold, as the session `shell:<pid>`; a short command never
//! does. The command line lives in memory and in `sum`; the history file holds hashes and
//! durations only, and the trace and audit log never see the text (`trace::redact_hook`).

use crate::hook_source::{branch_of, host_ctx, repo_of};
use crate::registry::{now_unix, HostCtx, Registry, SessionEvent, SessionPatch, Via};
use agentlink_proto::{Agent, SessState};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// A finished job's card stays this long unless dismissed.
const KEEP_FINISHED: Duration = Duration::from_secs(600);
/// How often the shells' pids are checked, so a killed terminal does not leave a card forever.
const LIVE_EVERY: Duration = Duration::from_secs(5);
/// Only runs at least this long (and successful) feed the estimate.
const MIN_RECORD: Duration = Duration::from_secs(5);
const KEEP_RUNS: usize = 5;
const MAX_KEYS: usize = 500;
/// Longest command line that can be typed again.
const MAX_RERUN_CHARS: usize = 2000;
const SIGINT_EXIT: u64 = 130;

/// Interactive programs: a card for them would be wrong the whole time they run.
const IGNORED: &[&str] = &[
    "vim", "nvim", "vi", "view", "vimdiff", "nano", "emacs", "less", "more", "man", "ssh", "mosh",
    "tmux", "screen", "top", "htop", "btop", "watch", "fg", "bg", "claude", "copilot", "codex",
    "aider", "gemini",
];
/// Ignored only when started without arguments (a REPL or a nested shell, not a script run).
const IGNORED_BARE: &[&str] = &[
    "python", "ipython", "node", "irb", "psql", "sqlite3", "lua", "ghci", "sh", "bash", "zsh",
    "fish", "dash", "ksh",
];
/// Words that only prepare the real command; the program name skips them.
const WRAPPERS: &[&str] = &["sudo", "env", "time", "nohup", "nice", "command", "exec"];
/// Builtins that open a compound command line without being what runs.
const PREAMBLE: &[&str] = &[
    "cd", "pushd", "popd", "export", "source", ".", "set", "unset",
];

pub struct Config {
    /// A job is shown once it has run this long.
    pub min: Duration,
    /// `jobs-ignore.txt` and `job-history.json` live here; None = neither.
    pub home: Option<PathBuf>,
    /// Whether a process still exists (replaced in tests).
    pub alive: fn(u32) -> bool,
}

impl Config {
    pub fn from_env() -> Self {
        let min = std::env::var("AGENTLINK_JOB_MIN_S")
            .ok()
            .and_then(|v| v.trim().parse::<f64>().ok())
            .filter(|v| v.is_finite() && *v >= 0.0)
            .unwrap_or(10.0);
        Config {
            min: Duration::from_secs_f64(min.min(86_400.0)),
            home: Some(crate::paths::home()),
            alive: pid_alive,
        }
    }
}

impl Default for Config {
    fn default() -> Self {
        Config {
            min: Duration::from_secs(10),
            home: None,
            alive: |_| true,
        }
    }
}

// ── command lines ──

/// Splits a command line into simple commands (on unquoted `;`, `&`, `|`), each a word list.
fn segments(cmd: &str) -> Vec<Vec<String>> {
    let (mut out, mut words, mut word) = (Vec::new(), Vec::new(), String::new());
    let (mut quote, mut escaped, mut has_word) = (None::<char>, false, false);
    let flush_word = |words: &mut Vec<String>, word: &mut String, has: &mut bool| {
        if *has {
            words.push(std::mem::take(word));
            *has = false;
        }
    };
    for c in cmd.chars() {
        if escaped {
            (escaped, has_word) = (false, true);
            word.push(c);
        } else if c == '\\' && quote != Some('\'') {
            escaped = true;
        } else if let Some(q) = quote {
            if c == q {
                quote = None;
            } else {
                word.push(c);
            }
        } else if c == '\'' || c == '"' {
            (quote, has_word) = (Some(c), true);
        } else if c.is_whitespace() {
            flush_word(&mut words, &mut word, &mut has_word);
        } else if matches!(c, ';' | '&' | '|') {
            flush_word(&mut words, &mut word, &mut has_word);
            if !words.is_empty() {
                out.push(std::mem::take(&mut words));
            }
        } else {
            has_word = true;
            word.push(c);
        }
    }
    flush_word(&mut words, &mut word, &mut has_word);
    if !words.is_empty() {
        out.push(words);
    }
    out
}

fn is_assignment(w: &str) -> bool {
    w.split_once('=').is_some_and(|(k, _)| {
        !k.is_empty()
            && !k.starts_with(|c: char| c.is_ascii_digit())
            && k.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
    })
}

/// Options of a wrapper that take a separate value.
fn takes_value(wrapper: &str, flag: &str) -> bool {
    match wrapper {
        "sudo" => matches!(
            flag,
            "-u" | "-g" | "-h" | "-p" | "-C" | "-D" | "-R" | "-T" | "-U"
        ),
        "nice" => flag == "-n",
        "env" => matches!(flag, "-u" | "-C" | "-S"),
        "time" => matches!(flag, "-f" | "-o"),
        "exec" => flag == "-a",
        _ => false,
    }
}

/// Program (basename) and the number of words after it, for one simple command: leading
/// `VAR=value` words and wrappers (with their options) are skipped.
fn program_of(words: &[String]) -> Option<(String, usize)> {
    let mut i = 0;
    loop {
        let w = words.get(i)?;
        if is_assignment(w) {
            i += 1;
            continue;
        }
        let base = w.rsplit(['/', '\\']).next().unwrap_or(w);
        if WRAPPERS.contains(&base) {
            i += 1;
            while let Some(f) = words.get(i).filter(|f| f.starts_with('-')) {
                i += 1 + usize::from(takes_value(base, f));
            }
            continue;
        }
        return (!base.is_empty()).then(|| (base.to_owned(), words.len() - i - 1));
    }
}

/// The program a command line runs, for the card's `tool`: the first simple command that is
/// not just a `cd`-style preamble.
pub fn program_name(cmd: &str) -> Option<String> {
    segments(cmd)
        .iter()
        .filter_map(|s| program_of(s))
        .find(|(p, _)| !PREAMBLE.contains(&p.as_str()))
        .map(|(p, _)| p)
}

/// `python3.12` is `python`.
fn normalize(program: &str) -> &str {
    match program.strip_prefix("python") {
        Some(rest) if rest.chars().all(|c| c.is_ascii_digit() || c == '.') => "python",
        _ => program,
    }
}

/// The user's extra program names: one per line, `#` comments.
pub fn parse_ignore(text: &str) -> Vec<String> {
    text.lines()
        .map(|l| l.split('#').next().unwrap_or("").trim())
        .filter(|l| !l.is_empty())
        .map(str::to_owned)
        .collect()
}

/// Whether any simple command of the line is an interactive program.
pub fn is_ignored(cmd: &str, extra: &[String]) -> bool {
    segments(cmd)
        .iter()
        .filter_map(|s| program_of(s))
        .any(|(p, nargs)| {
            let n = normalize(&p);
            IGNORED.contains(&n)
                || extra.iter().any(|e| e == &p || e == n)
                || (nargs == 0 && IGNORED_BARE.contains(&n))
        })
}

// ── history ──

fn fnv64(parts: &[&[u8]]) -> u64 {
    parts.iter().fold(0xcbf2_9ce4_8422_2325u64, |h, p| {
        p.iter()
            .chain(&[0u8])
            .fold(h, |h, b| (h ^ u64::from(*b)).wrapping_mul(0x100_0000_01b3))
    })
}

/// Stable key of a command line in a directory; the text itself is never stored.
pub fn history_key(cwd: Option<&str>, cmd: &str) -> String {
    format!(
        "{:016x}",
        fnv64(&[cwd.unwrap_or("").as_bytes(), cmd.as_bytes()])
    )
}

#[derive(Default)]
struct History {
    path: Option<PathBuf>,
    /// key → (last successful durations in seconds, recency counter)
    map: BTreeMap<String, (Vec<u64>, u64)>,
    seq: u64,
}

impl History {
    /// A missing or corrupt file is an empty history.
    fn load(path: Option<PathBuf>) -> Self {
        let mut h = History {
            path,
            ..Default::default()
        };
        let Some(v) = h
            .path
            .as_ref()
            .and_then(|p| std::fs::read_to_string(p).ok())
            .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        else {
            return h;
        };
        h.seq = v["seq"].as_u64().unwrap_or(0);
        for (k, e) in v["jobs"].as_object().into_iter().flatten() {
            let runs: Vec<u64> = e["d"]
                .as_array()
                .map(|a| a.iter().filter_map(Value::as_u64).collect())
                .unwrap_or_default();
            if !runs.is_empty() {
                let u = e["u"].as_u64().unwrap_or(0);
                h.seq = h.seq.max(u);
                h.map.insert(
                    k.clone(),
                    (runs[runs.len().saturating_sub(KEEP_RUNS)..].to_vec(), u),
                );
            }
        }
        h
    }

    /// Median of the recorded runs, when there are at least two.
    fn eta(&self, key: &str) -> Option<u64> {
        let mut r = self.map.get(key)?.0.clone();
        if r.len() < 2 {
            return None;
        }
        r.sort_unstable();
        let m = r.len() / 2;
        Some(if r.len() % 2 == 1 {
            r[m]
        } else {
            (r[m - 1] + r[m]) / 2
        })
    }

    fn record(&mut self, key: &str, secs: u64) {
        self.seq += 1;
        let e = self.map.entry(key.to_owned()).or_default();
        e.0.push(secs);
        let extra = e.0.len().saturating_sub(KEEP_RUNS);
        e.0.drain(..extra);
        e.1 = self.seq;
        while self.map.len() > MAX_KEYS {
            let Some(old) = self
                .map
                .iter()
                .min_by_key(|(_, (_, u))| *u)
                .map(|(k, _)| k.clone())
            else {
                break;
            };
            self.map.remove(&old);
        }
        self.save();
    }

    /// Temp file + rename, so a crash never leaves half a file.
    fn save(&self) {
        let Some(path) = &self.path else { return };
        let jobs: serde_json::Map<String, Value> = self
            .map
            .iter()
            .map(|(k, (d, u))| (k.clone(), json!({"d": d, "u": u})))
            .collect();
        let text = json!({"v": 1, "seq": self.seq, "jobs": jobs}).to_string();
        let tmp = path.with_extension("json.tmp");
        let res = path
            .parent()
            .map_or(Ok(()), std::fs::create_dir_all)
            .and_then(|_| std::fs::write(&tmp, text))
            .and_then(|_| std::fs::rename(&tmp, path));
        if let Err(e) = res {
            tracing::warn!("job history not saved: {e}");
        }
    }
}

// ── processes ──

fn pid_alive(pid: u32) -> bool {
    #[cfg(target_os = "linux")]
    {
        std::path::Path::new(&format!("/proc/{pid}")).exists()
    }
    #[cfg(all(unix, not(target_os = "linux")))]
    {
        std::process::Command::new("kill")
            .args(["-0", &pid.to_string()])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(true)
    }
    #[cfg(not(unix))]
    {
        let _ = pid;
        true
    }
}

/// `pgrp` and `tpgid` out of a `/proc/<pid>/stat` line (the command name may hold spaces and
/// parentheses, so fields count from the last `)`).
#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
fn parse_stat(stat: &str) -> Option<(i64, i64)> {
    let rest = stat.get(stat.rfind(')')? + 1..)?;
    let mut f = rest.split_whitespace();
    let pgrp = f.nth(2)?.parse().ok()?;
    let tpgid = f.nth(2)?.parse().ok()?;
    Some((pgrp, tpgid))
}

/// `pgid` and `tpgid` columns of `ps -o pgid=,tpgid=`.
fn parse_ps(out: &str) -> Option<(i64, i64)> {
    let mut f = out.split_whitespace();
    Some((f.next()?.parse().ok()?, f.next()?.parse().ok()?))
}

/// The shell's own process group and the foreground group of its tty.
#[cfg(unix)]
fn groups_of(pid: u32) -> Option<(i64, i64)> {
    #[cfg(target_os = "linux")]
    if let Some(g) = std::fs::read_to_string(format!("/proc/{pid}/stat"))
        .ok()
        .and_then(|s| parse_stat(&s))
    {
        return Some(g);
    }
    let out = std::process::Command::new("ps")
        .args(["-o", "pgid=,tpgid=", "-p", &pid.to_string()])
        .stdin(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .output()
        .ok()?;
    parse_ps(&String::from_utf8_lossy(&out.stdout))
}

#[derive(Debug, PartialEq)]
pub enum CancelErr {
    /// Nothing but the shell itself runs in its terminal.
    Stale,
    #[cfg_attr(unix, allow(dead_code))]
    Unsupported,
    Failed(String),
}

/// Ctrl-C for the shell's terminal: SIGINT to its foreground process group.
pub fn interrupt_foreground(shell_pid: u32) -> Result<(), CancelErr> {
    #[cfg(unix)]
    {
        let (own, fg) = groups_of(shell_pid).ok_or(CancelErr::Stale)?;
        if fg <= 1 || fg == own {
            return Err(CancelErr::Stale);
        }
        let st = std::process::Command::new("kill")
            .args(["-INT", "--", &format!("-{fg}")])
            .stdin(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map_err(|e| CancelErr::Failed(e.to_string()))?;
        st.success()
            .then_some(())
            .ok_or_else(|| CancelErr::Failed("kill failed".into()))
    }
    #[cfg(not(unix))]
    {
        let _ = shell_pid;
        Err(CancelErr::Unsupported)
    }
}

// ── the jobs themselves ──

struct Run {
    cmd: String,
    cwd: Option<String>,
    ctx: HostCtx,
    started: Instant,
    t0: u64,
    /// The shell's count of reported command lines (None from an older snippet).
    seq: Option<u64>,
    hist: String,
    /// Interactive program: tracked, never shown.
    ignored: bool,
    /// Shown as a session.
    surfaced: bool,
    /// An agent's own card covers this terminal.
    covered: bool,
}

struct Finished {
    /// The line to type again; None when it cannot be (control characters, too long).
    rerun: Option<String>,
    ended: Instant,
}

#[derive(Default)]
struct Shell {
    run: Option<Run>,
    finished: Option<Finished>,
    /// Highest seq that has ended: the detached shim calls can arrive out of order, so a
    /// start at or below it is a command that already finished.
    ended_seq: Option<u64>,
}

struct State {
    shells: HashMap<u32, Shell>,
    history: History,
    last_live: Instant,
}

#[derive(Debug, PartialEq)]
pub enum RerunErr {
    /// Not a finished job of a known shell.
    Stale,
    /// The shell runs something.
    Busy,
    /// The line has newlines or control characters, or is very long.
    Untypable,
}

pub struct Jobs {
    reg: Arc<Registry>,
    cfg: Config,
    state: Mutex<State>,
}

fn key_of(pid: u32) -> String {
    format!("shell:{pid}")
}

impl Jobs {
    pub fn new(reg: Arc<Registry>, cfg: Config) -> Self {
        let history = History::load(cfg.home.as_ref().map(|h| h.join("job-history.json")));
        Jobs {
            reg,
            cfg,
            state: Mutex::new(State {
                shells: HashMap::new(),
                history,
                last_live: Instant::now(),
            }),
        }
    }

    /// Checks the clock (threshold, 10-minute removal, liveness) a few times a second.
    pub fn spawn_ticker(self: &Arc<Self>) {
        let jobs = self.clone();
        let every = (jobs.cfg.min / 2).clamp(Duration::from_millis(50), Duration::from_millis(500));
        tokio::spawn(async move {
            let mut t = tokio::time::interval(every);
            loop {
                t.tick().await;
                jobs.tick(Instant::now());
            }
        });
    }

    fn ignore_list(&self) -> Vec<String> {
        self.cfg
            .home
            .as_ref()
            .and_then(|h| std::fs::read_to_string(h.join("jobs-ignore.txt")).ok())
            .map(|t| parse_ignore(&t))
            .unwrap_or_default()
    }

    /// One `agentlink-hook shell …` line.
    pub fn on_line(&self, v: &Value) {
        let p = &v["payload"];
        let Some(pid) = p["pid"].as_u64().and_then(|p| u32::try_from(p).ok()) else {
            return;
        };
        let now = Instant::now();
        match v["event"].as_str() {
            Some("start") => {
                let Some(cmd) = p["cmd"].as_str().map(str::trim).filter(|c| !c.is_empty()) else {
                    return;
                };
                let cwd = p["cwd"]
                    .as_str()
                    .or_else(|| v["cwd"].as_str())
                    .filter(|c| !c.is_empty())
                    .map(str::to_owned);
                let ctx = host_ctx(v, cwd);
                self.start_seq(now, now_unix(), pid, cmd, ctx, p["seq"].as_u64());
            }
            Some("end") => {
                if let Some(exit) = p["exit"].as_u64() {
                    self.end_seq(now, pid, exit, p["seq"].as_u64());
                }
            }
            Some("exit") => self.shell_exit(pid),
            _ => {}
        }
    }

    /// Without a seq, as an older snippet reports.
    #[cfg(test)]
    pub fn start(&self, now: Instant, t0: u64, pid: u32, cmd: &str, ctx: HostCtx) {
        self.start_seq(now, t0, pid, cmd, ctx, None);
    }

    pub fn start_seq(
        &self,
        now: Instant,
        t0: u64,
        pid: u32,
        cmd: &str,
        ctx: HostCtx,
        seq: Option<u64>,
    ) {
        let ignored = is_ignored(cmd, &self.ignore_list());
        let mut st = self.state.lock().unwrap();
        let sh = st.shells.entry(pid).or_default();
        if let Some(n) = seq {
            let newer_running = sh.run.as_ref().and_then(|r| r.seq).is_some_and(|r| r > n);
            if sh.ended_seq.is_some_and(|e| e >= n) || newer_running {
                return; // it already finished, or a later line is what runs
            }
        }
        // A start with no end before it (the end line was lost): what showed is over.
        if sh.run.take().is_some_and(|r| r.surfaced) {
            self.reg.apply(SessionEvent::End { key: key_of(pid) });
            sh.finished = None;
        }
        sh.run = Some(Run {
            hist: history_key(ctx.cwd.as_deref(), cmd),
            cmd: cmd.to_owned(),
            cwd: ctx.cwd.clone(),
            ctx,
            started: now,
            t0,
            seq,
            ignored,
            surfaced: false,
            covered: false,
        });
    }

    /// Shows the job as a session (once it has run long enough).
    fn surface(&self, history: &History, pid: u32, sh: &mut Shell) {
        let Some(run) = sh.run.as_mut() else { return };
        if run.surfaced || run.ignored || run.covered {
            return;
        }
        if run
            .ctx
            .tty
            .as_deref()
            .is_some_and(|t| self.reg.tty_hosts_agent(t))
        {
            run.covered = true;
            return;
        }
        let mut p = SessionPatch::new(Agent::Shell);
        p.state = Some(SessState::Work);
        p.tool = program_name(&run.cmd);
        p.summary = Some(run.cmd.clone());
        if let Some(cwd) = &run.cwd {
            p.repo = repo_of(cwd);
            p.branch = branch_of(cwd);
        }
        p.t0 = Some(run.t0);
        p.eta = Some(history.eta(&run.hist).unwrap_or(0));
        self.reg.apply(SessionEvent::Update {
            key: key_of(pid),
            via: Via::Local,
            patch: p,
            ctx: run.ctx.clone(),
        });
        run.surfaced = true;
        sh.finished = None; // the new job takes the card over
    }

    /// Without a seq, as an older snippet reports.
    #[cfg(test)]
    pub fn end(&self, now: Instant, pid: u32, exit: u64) {
        self.end_seq(now, pid, exit, None);
    }

    pub fn end_seq(&self, now: Instant, pid: u32, exit: u64, seq: Option<u64>) {
        let mut st = self.state.lock().unwrap();
        let State {
            shells, history, ..
        } = &mut *st;
        if let Some(n) = seq {
            let sh = shells.entry(pid).or_default();
            sh.ended_seq = Some(sh.ended_seq.map_or(n, |e| e.max(n)));
            match sh.run.as_ref().and_then(|r| r.seq) {
                Some(r) if r > n => return, // a later line is what runs
                Some(r) if r < n => {
                    // the shell runs one line at a time: an older one cannot still be running
                    if sh.run.take().is_some_and(|r| r.surfaced) {
                        self.reg.apply(SessionEvent::End { key: key_of(pid) });
                        sh.finished = None;
                    }
                    return;
                }
                _ => {}
            }
        }
        let Some(sh) = shells.get_mut(&pid) else {
            return;
        };
        let Some(run) = sh.run.as_ref() else { return };
        let dur = now.saturating_duration_since(run.started);
        // the ticker may not have seen the threshold pass yet
        if dur >= self.cfg.min {
            self.surface(history, pid, sh);
        }
        let run = sh.run.take().expect("checked above");
        if exit == 0 && !run.ignored && dur >= MIN_RECORD {
            history.record(&run.hist, dur.as_secs());
        }
        if run.surfaced {
            if exit == SIGINT_EXIT {
                // the user is at the keyboard, or pressed Stop: no buzz, no card
                self.reg.apply(SessionEvent::End { key: key_of(pid) });
            } else {
                let mut p = SessionPatch::new(Agent::Shell);
                p.state = Some(if exit == 0 {
                    SessState::Done
                } else {
                    SessState::Error
                });
                p.exit = Some(exit);
                self.reg.apply(SessionEvent::Update {
                    key: key_of(pid),
                    via: Via::Local,
                    patch: p,
                    ctx: run.ctx.clone(),
                });
                let typable = run.cmd.chars().count() <= MAX_RERUN_CHARS
                    && !run.cmd.chars().any(char::is_control);
                sh.finished = Some(Finished {
                    rerun: typable.then(|| run.cmd.clone()),
                    ended: now,
                });
            }
        }
        if sh.run.is_none() && sh.finished.is_none() && sh.ended_seq.is_none() {
            shells.remove(&pid);
        }
    }

    /// The shell itself exited.
    pub fn shell_exit(&self, pid: u32) {
        self.state.lock().unwrap().shells.remove(&pid);
        self.reg.apply(SessionEvent::End { key: key_of(pid) });
    }

    pub fn tick(&self, now: Instant) {
        let check_live = {
            let mut st = self.state.lock().unwrap();
            let State {
                shells, history, ..
            } = &mut *st;
            for (pid, sh) in shells.iter_mut() {
                if sh
                    .run
                    .as_ref()
                    .is_some_and(|r| now.saturating_duration_since(r.started) >= self.cfg.min)
                {
                    self.surface(history, *pid, sh);
                }
                if sh
                    .finished
                    .as_ref()
                    .is_some_and(|f| now.saturating_duration_since(f.ended) >= KEEP_FINISHED)
                {
                    sh.finished = None;
                    self.reg.apply(SessionEvent::End { key: key_of(*pid) });
                }
            }
            shells.retain(|_, sh| {
                sh.run.is_some() || sh.finished.is_some() || sh.ended_seq.is_some()
            });
            let due = now.saturating_duration_since(st.last_live) >= LIVE_EVERY;
            if due {
                st.last_live = now;
            }
            due.then(|| st.shells.keys().copied().collect::<Vec<_>>())
        };
        // the pid checks may spawn a process: not under the lock
        for pid in check_live.into_iter().flatten() {
            if !(self.cfg.alive)(pid) {
                self.shell_exit(pid);
            }
        }
    }

    /// The shell pid behind a job session.
    fn shell_of(&self, st: &State, sid: u64) -> Option<u32> {
        st.shells
            .keys()
            .copied()
            .find(|pid| self.reg.sid_of(&key_of(*pid)) == Some(sid))
    }

    pub fn shell_pid(&self, sid: u64) -> Option<u32> {
        self.shell_of(&self.state.lock().unwrap(), sid)
    }

    /// Device `ack` on a finished job: the card is removed. A running job is left to the
    /// ordinary ack.
    pub fn dismiss(&self, sid: u64) -> bool {
        let mut st = self.state.lock().unwrap();
        let Some(pid) = self.shell_of(&st, sid) else {
            return false;
        };
        let sh = st.shells.get_mut(&pid).expect("found above");
        if sh.finished.take().is_none() {
            return false;
        }
        self.reg.apply(SessionEvent::End { key: key_of(pid) });
        if sh.run.is_none() && sh.ended_seq.is_none() {
            st.shells.remove(&pid);
        }
        true
    }

    /// The command line of a finished job that may be typed again.
    pub fn rerun_text(&self, sid: u64) -> Result<String, RerunErr> {
        let st = self.state.lock().unwrap();
        let sh = self
            .shell_of(&st, sid)
            .and_then(|pid| st.shells.get(&pid))
            .ok_or(RerunErr::Stale)?;
        let fin = sh.finished.as_ref().ok_or(RerunErr::Stale)?;
        if sh.run.is_some() {
            return Err(RerunErr::Busy);
        }
        fin.rerun.clone().ok_or(RerunErr::Untypable)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::registry::Update;
    use crate::trace::Trace;
    use agentlink_proto::Session;
    use tokio::sync::broadcast::Receiver;

    struct Rig {
        jobs: Jobs,
        reg: Arc<Registry>,
        rx: Receiver<Update>,
        t: Instant,
        dir: PathBuf,
    }

    fn rig(name: &str) -> Rig {
        let dir =
            std::env::temp_dir().join(format!("agentlink-jobs-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let reg = Arc::new(Registry::new(Trace::null()));
        let (_, rx) = reg.subscribe();
        let cfg = Config {
            home: Some(dir.clone()),
            ..Default::default()
        };
        Rig {
            jobs: Jobs::new(reg.clone(), cfg),
            reg,
            rx,
            t: Instant::now(),
            dir,
        }
    }

    impl Rig {
        fn at(&self, secs: u64) -> Instant {
            self.t + Duration::from_secs(secs)
        }
        fn start(&self, pid: u32, cmd: &str, tty: Option<&str>) {
            let ctx = HostCtx {
                cwd: Some("/nonexistent/proj".into()),
                tty: tty.map(str::to_owned),
                ..Default::default()
            };
            self.jobs.start(self.t, 1000, pid, cmd, ctx);
        }
        fn upd(&mut self) -> Option<Session> {
            match self.rx.try_recv() {
                Ok(Update::Upsert(s)) => Some(s),
                Ok(_) => panic!("not an upsert"),
                Err(_) => None,
            }
        }
        fn drain(&mut self) {
            while self.rx.try_recv().is_ok() {}
        }
        fn gone(&mut self) -> bool {
            matches!(self.rx.try_recv(), Ok(Update::Gone(_)))
        }
        fn quiet(&mut self) -> bool {
            self.rx.try_recv().is_err()
        }
    }

    impl Drop for Rig {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.dir);
        }
    }

    #[test]
    fn program_names_skip_assignments_and_wrappers() {
        let p = |c: &str| program_name(c);
        assert_eq!(p("cargo build --release").as_deref(), Some("cargo"));
        assert_eq!(p("FOO=1 BAR=\"a b\" make -j8").as_deref(), Some("make"));
        assert_eq!(
            p("sudo -u bob nice -n 5 /usr/bin/npm test").as_deref(),
            Some("npm")
        );
        assert_eq!(p("env -i A=b time -p ./run.sh").as_deref(), Some("run.sh"));
        assert_eq!(
            p("nohup command exec python3 x.py").as_deref(),
            Some("python3")
        );
        assert_eq!(p("cd ~/src && cargo test").as_deref(), Some("cargo"));
        assert_eq!(p("'my prog' --x").as_deref(), Some("my prog"));
        assert_eq!(p("FOO=1"), None);
        assert_eq!(p("   "), None);
    }

    #[test]
    fn ignore_list_covers_interactive_programs() {
        let none: &[String] = &[];
        for c in [
            "vim a.txt",
            "sudo nvim /etc/hosts",
            "less log",
            "git log | less",
            "cd x && vim",
            "ssh host",
            "tmux attach",
            "fg",
            "claude --resume",
            "python",
            "python3.12",
            "FOO=1 node",
            "psql",
            "bash",
            "watch -n1 ls",
        ] {
            assert!(is_ignored(c, none), "{c}");
        }
        for c in [
            "python3 build.py",
            "node server.js",
            "psql -f x.sql",
            "cargo build",
            "tail -f log",
            "bash script.sh",
            "echo vim",
        ] {
            assert!(!is_ignored(c, none), "{c}");
        }
        let extra = parse_ignore("# mine\nmytui   # my editor\n\nfoo\n");
        assert_eq!(extra, vec!["mytui", "foo"]);
        assert!(is_ignored("sudo mytui --x", &extra));
        assert!(!is_ignored("mytui2", &extra));
    }

    #[test]
    fn short_commands_never_reach_a_device() {
        let mut r = rig("short");
        r.start(7, "ls -la", None);
        r.jobs.tick(r.at(9));
        r.jobs.end(r.at(9), 7, 0);
        assert!(r.quiet());
        r.start(7, "false", None);
        r.jobs.end(r.at(3), 7, 1);
        assert!(r.quiet());
        assert!(r.reg.snapshot().is_empty());
        assert_eq!(r.jobs.rerun_text(1), Err(RerunErr::Stale));
    }

    #[test]
    fn a_long_command_surfaces_once_past_the_threshold() {
        let mut r = rig("long");
        r.start(7, "cargo build --release", Some("/dev/pts/3"));
        r.jobs.tick(r.at(5));
        assert!(r.quiet());
        r.jobs.tick(r.at(10));
        let s = r.upd().expect("surfaced");
        assert_eq!(
            (s.ag, s.st, s.t0, s.eta, s.tool.as_deref()),
            (
                Some(Agent::Shell),
                Some(SessState::Work),
                Some(1000),
                Some(0),
                Some("cargo")
            )
        );
        assert_eq!(s.sum.as_deref(), Some("cargo build --release"));
        assert_eq!(s.repo.as_deref(), Some("proj"));
        r.jobs.tick(r.at(11));
        assert!(r.quiet(), "once");
    }

    #[test]
    fn end_states_done_error_and_interrupt() {
        let mut r = rig("ends");
        r.start(7, "make", None);
        r.jobs.tick(r.at(10));
        r.upd().unwrap();
        r.jobs.end(r.at(42), 7, 0);
        let d = r.upd().unwrap();
        assert_eq!(
            (d.st, d.exit, d.seen),
            (Some(SessState::Done), Some(0), Some(false))
        );
        assert_eq!(r.jobs.rerun_text(1), Ok("make".into()));

        // the same shell's next job takes the card over, same sid
        r.jobs
            .start(r.at(50), 2000, 7, "make test", HostCtx::default());
        r.jobs.tick(r.at(60));
        let s = r.upd().unwrap();
        assert_eq!(
            (s.sid, s.st, s.t0, s.eta),
            (1, Some(SessState::Work), Some(2000), Some(0))
        );
        assert_eq!(r.reg.snapshot()[0].exit, None, "last status cleared");
        assert_eq!(r.jobs.rerun_text(1), Err(RerunErr::Stale), "running again");
        r.jobs.end(r.at(70), 7, 101);
        let e = r.upd().unwrap();
        assert_eq!((e.st, e.exit), (Some(SessState::Error), Some(101)));

        // exit 130: removed, no done / error
        r.jobs
            .start(r.at(80), 3000, 7, "sleep 100", HostCtx::default());
        r.jobs.tick(r.at(90));
        r.upd().unwrap();
        r.jobs.end(r.at(95), 7, 130);
        assert!(r.gone());
        assert!(r.quiet());
        assert!(r.reg.snapshot().is_empty());
    }

    #[test]
    fn a_job_that_ends_just_after_the_threshold_still_counts() {
        let mut r = rig("late");
        r.start(7, "make", None);
        r.jobs.end(r.at(11), 7, 0); // the ticker never ran
        assert_eq!(r.upd().unwrap().st, Some(SessState::Work));
        assert_eq!(r.upd().unwrap().st, Some(SessState::Done));
    }

    #[test]
    fn ignored_programs_are_tracked_but_never_shown() {
        let mut r = rig("ignored");
        r.start(7, "vim notes.md", None);
        r.jobs.tick(r.at(600));
        assert!(r.quiet());
        r.jobs.end(r.at(601), 7, 0);
        assert!(r.quiet());
        assert!(r.jobs.state.lock().unwrap().shells.is_empty());
        std::fs::write(r.dir.join("jobs-ignore.txt"), "make\n").unwrap();
        r.start(7, "make all", None);
        r.jobs.tick(r.at(20));
        assert!(r.quiet(), "from the user's list");
    }

    #[test]
    fn a_terminal_with_an_agent_is_left_to_the_agents_card() {
        let mut r = rig("agent");
        let mut p = SessionPatch::new(Agent::Claude);
        p.state = Some(SessState::Work);
        r.reg.apply(SessionEvent::Update {
            key: "claude:a".into(),
            via: Via::Local,
            patch: p,
            ctx: HostCtx {
                tty: Some("/dev/pts/9".into()),
                ..Default::default()
            },
        });
        r.upd().unwrap();
        r.start(7, "npm run build", Some("/dev/pts/9"));
        r.jobs.tick(r.at(30));
        assert!(r.quiet());
        r.start(8, "npm run build", Some("/dev/pts/10"));
        r.jobs.tick(r.at(30));
        assert_eq!(r.upd().unwrap().ag, Some(Agent::Shell));
    }

    #[test]
    fn dismiss_timeout_shell_exit_and_dead_shell_remove_the_card() {
        let mut r = rig("remove");
        let finish = |r: &mut Rig, pid| {
            r.start(pid, "make", None);
            r.jobs.tick(r.at(10));
            r.jobs.end(r.at(12), pid, 0);
            r.drain();
        };
        finish(&mut r, 7);
        assert!(!r.jobs.dismiss(99));
        assert!(r.jobs.dismiss(1));
        assert!(r.gone());
        assert!(!r.jobs.dismiss(1));

        finish(&mut r, 8);
        r.jobs.tick(r.at(12 + 599));
        assert!(r.quiet());
        r.jobs.tick(r.at(12 + 600));
        assert!(r.gone());

        finish(&mut r, 9);
        r.jobs.shell_exit(9);
        assert!(r.gone());

        // a running job's ack is the ordinary one
        r.start(10, "make", None);
        r.jobs.tick(r.at(10));
        r.upd().unwrap();
        assert!(!r.jobs.dismiss(4));

        // a killed terminal: the liveness check removes the card
        let reg = Arc::new(Registry::new(Trace::null()));
        let (_, mut rx) = reg.subscribe();
        let jobs = Jobs::new(
            reg,
            Config {
                alive: |_| false,
                ..Default::default()
            },
        );
        let t = Instant::now();
        jobs.start(t, 1, 5, "make", HostCtx::default());
        jobs.tick(t + Duration::from_secs(10));
        assert!(matches!(rx.try_recv(), Ok(Update::Upsert(_))));
        jobs.tick(t + Duration::from_secs(10 + 5));
        assert!(matches!(rx.try_recv(), Ok(Update::Gone(_))));
    }

    #[test]
    fn rerun_is_refused_while_busy_and_for_untypable_lines() {
        let r = rig("rerun");
        r.start(7, "make", None);
        r.jobs.tick(r.at(10));
        assert_eq!(r.jobs.rerun_text(1), Err(RerunErr::Stale), "not finished");
        r.jobs.end(r.at(20), 7, 0);
        assert_eq!(r.jobs.rerun_text(1), Ok("make".into()));
        // a new, still short command: the shell is busy
        r.jobs.start(r.at(21), 1, 7, "ls", HostCtx::default());
        assert_eq!(r.jobs.rerun_text(1), Err(RerunErr::Busy));
        r.jobs.end(r.at(22), 7, 0);
        assert_eq!(r.jobs.rerun_text(1), Ok("make".into()));
        r.jobs.dismiss(1);
        r.jobs
            .start(r.at(30), 1, 7, "cat <<EOF\nx\nEOF", HostCtx::default());
        r.jobs.end(r.at(60), 7, 0);
        assert_eq!(
            r.jobs.rerun_text(2),
            Err(RerunErr::Untypable),
            "a new card, a new sid"
        );
        assert_eq!(r.jobs.shell_pid(2), Some(7));
        assert_eq!(r.jobs.shell_pid(5), None);
    }

    #[test]
    fn history_median_needs_two_runs_and_keeps_five() {
        let mut r = rig("hist");
        let run = |r: &mut Rig, secs: u64, exit: u64| {
            r.start(7, "make", None);
            r.jobs.end(r.at(secs), 7, exit);
            r.drain();
        };
        run(&mut r, 20, 0);
        r.start(7, "make", None);
        r.jobs.tick(r.at(10));
        assert_eq!(r.upd().unwrap().eta, Some(0), "one run is not an estimate");
        r.jobs.end(r.at(30), 7, 0);
        r.jobs.dismiss(1);
        r.drain();
        r.start(7, "make", None);
        r.jobs.tick(r.at(10));
        assert_eq!(r.upd().unwrap().eta, Some(25), "median of 20 and 30");
        r.jobs.end(r.at(11), 7, 1); // failures and short runs are not recorded
        run(&mut r, 3, 0);
        for secs in [40, 50, 60, 70, 80] {
            run(&mut r, secs, 0);
        }
        let key = history_key(Some("/nonexistent/proj"), "make");
        assert_eq!(
            r.jobs.state.lock().unwrap().history.map[&key].0,
            vec![40, 50, 60, 70, 80]
        );
        assert_eq!(r.jobs.state.lock().unwrap().history.eta(&key), Some(60));
        // survives a restart, and holds no command text
        let text = std::fs::read_to_string(r.dir.join("job-history.json")).unwrap();
        assert!(!text.contains("make") && !text.contains("proj"));
        let again = History::load(Some(r.dir.join("job-history.json")));
        assert_eq!(again.eta(&key), Some(60));
    }

    #[test]
    fn history_is_capped_and_tolerates_a_bad_file() {
        let dir = std::env::temp_dir().join(format!("agentlink-hist-cap-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let path = dir.join("job-history.json");
        let mut h = History::load(Some(path.clone())); // missing
        for i in 0..(MAX_KEYS + 20) {
            h.record(&format!("k{i:04}"), 10);
        }
        assert_eq!(h.map.len(), MAX_KEYS);
        assert!(!h.map.contains_key("k0000") && h.map.contains_key("k0519"));
        h.record("k0100", 11); // touched: now the newest
        h.record("new", 10);
        assert!(h.map.contains_key("k0100") && !h.map.contains_key("k0020"));
        assert!(!path.with_extension("json.tmp").exists());
        std::fs::write(&path, "{ not json").unwrap();
        assert!(History::load(Some(path.clone())).map.is_empty());
        std::fs::write(&path, r#"{"jobs":{"a":{"d":"x"},"b":{"d":[5,6],"u":3}}}"#).unwrap();
        let h = History::load(Some(path));
        assert_eq!(h.map.len(), 1);
        assert_eq!(h.eta("b"), Some(5));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn hook_lines_drive_the_same_paths() {
        let reg = Arc::new(Registry::new(Trace::null()));
        let (_, mut rx) = reg.subscribe();
        let jobs = Jobs::new(
            reg.clone(),
            Config {
                min: Duration::ZERO,
                ..Default::default()
            },
        );
        let line = |ev: &str, p: Value| {
            json!({"agent":"shell","event":ev,"cwd":"/hook/dir","tty":"/dev/pts/4",
                   "pids":[11,12],"env":{"KITTY_WINDOW_ID":"3"},"payload":p})
        };
        // lines without what they need are dropped
        jobs.on_line(&line("start", json!({"cmd": "make"})));
        jobs.on_line(&line("start", json!({"pid": 7, "cmd": "  "})));
        jobs.on_line(&line("end", json!({"pid": 7})));
        jobs.on_line(&line("exit", json!({"pid": 7})));
        assert!(rx.try_recv().is_err());
        jobs.on_line(&line(
            "start",
            json!({"pid": 7, "cmd": " make ", "cwd": "/nonexistent/proj"}),
        ));
        jobs.tick(Instant::now());
        let Ok(Update::Upsert(s)) = rx.try_recv() else {
            panic!("surfaced")
        };
        assert_eq!(s.sum.as_deref(), Some("make"));
        assert_eq!(s.repo.as_deref(), Some("proj"));
        let crate::registry::Route::Ready(_, ctx) = reg.route(s.sid) else {
            panic!()
        };
        assert_eq!(ctx.tty.as_deref(), Some("/dev/pts/4"));
        assert_eq!(ctx.env["KITTY_WINDOW_ID"], "3");
        assert_eq!(ctx.pids, vec![11, 12]);
        jobs.on_line(&line("end", json!({"pid": 7, "exit": 2})));
        let Ok(Update::Upsert(e)) = rx.try_recv() else {
            panic!()
        };
        assert_eq!((e.st, e.exit), (Some(SessState::Error), Some(2)));
        jobs.on_line(&line("exit", json!({"pid": 7})));
        assert!(matches!(rx.try_recv(), Ok(Update::Gone(_))));
    }

    fn seq_start(r: &Rig, pid: u32, cmd: &str, seq: u64) {
        r.jobs
            .start_seq(r.t, 1000, pid, cmd, HostCtx::default(), Some(seq));
    }

    #[test]
    fn end_before_start_leaves_nothing_running() {
        let mut r = rig("seq-swap");
        r.jobs.end_seq(r.at(1), 7, 0, Some(1)); // arrives first
        seq_start(&r, 7, "true", 1); // late: already finished
        r.jobs.tick(r.at(30));
        assert!(r.quiet());
        assert!(r.jobs.state.lock().unwrap().shells[&7].run.is_none());
    }

    #[test]
    fn swapped_starts_keep_the_later_command() {
        let mut r = rig("seq-starts");
        seq_start(&r, 7, "sleep 30", 2);
        seq_start(&r, 7, "true", 1); // late
        r.jobs.tick(r.at(10));
        assert_eq!(r.upd().unwrap().tool.as_deref(), Some("sleep"));
        // the late end of the first line does not end the second
        r.jobs.end_seq(r.at(11), 7, 0, Some(1));
        assert!(r.quiet());
        r.jobs.end_seq(r.at(12), 7, 0, Some(2));
        assert_eq!(r.upd().unwrap().st, Some(SessState::Done));
    }

    #[test]
    fn the_end_of_a_later_line_drops_an_older_running_one() {
        let mut r = rig("seq-late");
        seq_start(&r, 7, "make", 1);
        r.jobs.tick(r.at(10));
        r.upd().unwrap();
        r.jobs.end_seq(r.at(12), 7, 0, Some(2)); // line 1's end was lost
        assert!(r.gone());
        assert!(r.jobs.state.lock().unwrap().shells[&7].run.is_none());
        assert_eq!(r.jobs.rerun_text(1), Err(RerunErr::Stale));
    }

    #[test]
    fn lines_without_seq_behave_as_before() {
        let mut r = rig("seq-none");
        r.jobs.start(r.t, 1000, 7, "make", HostCtx::default());
        r.jobs.end_seq(r.at(12), 7, 0, None);
        r.jobs.start(r.t, 1000, 7, "make", HostCtx::default());
        r.jobs.start(r.t, 1000, 7, "make", HostCtx::default());
        // a mixed pair: an end with a seq finishes a seq-less start
        r.jobs.end_seq(r.at(13), 7, 0, Some(5));
        assert!(r.jobs.state.lock().unwrap().shells[&7].run.is_none());
        r.drain();
    }

    #[test]
    fn stat_and_ps_parsing() {
        assert_eq!(
            parse_stat("1234 (my (shell) x) S 1000 1234 1234 34817 5678 4194304 1 0"),
            Some((1234, 5678))
        );
        assert_eq!(parse_stat("garbage"), None);
        assert_eq!(parse_ps("  4242  4242\n"), Some((4242, 4242)));
        assert_eq!(parse_ps(""), None);
    }

    #[test]
    fn config_reads_fractional_thresholds() {
        std::env::set_var("AGENTLINK_JOB_MIN_S", "0.25");
        assert_eq!(Config::from_env().min, Duration::from_millis(250));
        std::env::set_var("AGENTLINK_JOB_MIN_S", "nope");
        assert_eq!(Config::from_env().min, Duration::from_secs(10));
        std::env::remove_var("AGENTLINK_JOB_MIN_S");
        assert_eq!(Config::from_env().min, Duration::from_secs(10));
    }
}
