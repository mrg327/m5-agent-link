//! Keep the computer awake while work runs (docs/design/PRESENCE.md section 5): a machine that goes to
//! sleep while an agent works or a job runs is the silent failure of stepping away.
//!
//! The daemon holds an *idle-sleep* inhibitor while any session is working or waiting on the user, or a
//! job runs, depending on the setting (`off`, `ac` = on AC power only, `always`). It does not block
//! lid-close suspend, the power key or screen locking: on Linux the logind lock is `idle` only, on macOS
//! `caffeinate -i` (idle sleep only).
//!
//! The inhibitor is a child process group that holds the lock for as long as it lives (`systemd-inhibit`,
//! `caffeinate`). What it runs ends within two seconds of the daemon's pid being gone
//! (`caffeinate -w` does so itself), so a daemon that is killed outright still releases it; a normal
//! release ends the whole group at once.
//!
//! **Test only:** `AGENTLINK_PRESENCE_FILE` may carry `power=ac|battery` to stand in for the power source.

use crate::registry::{now_unix, Registry};
use agentlink_proto::{SessState, Session};
use serde_json::{json, Value};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Off,
    /// Only on AC power (the default); a machine with no battery counts as on AC.
    Ac,
    Always,
}

impl Mode {
    pub fn name(self) -> &'static str {
        match self {
            Mode::Off => "off",
            Mode::Ac => "ac",
            Mode::Always => "always",
        }
    }
}

/// `off` / `ac` / `always` (also `on`, `battery`, `0`...). The first line that is not blank or a comment.
pub fn parse_mode(text: &str) -> Option<Mode> {
    let line = text
        .lines()
        .map(str::trim)
        .find(|l| !l.is_empty() && !l.starts_with('#'))?;
    match line
        .split_whitespace()
        .next()?
        .to_ascii_lowercase()
        .as_str()
    {
        "off" | "no" | "0" | "false" | "never" => Some(Mode::Off),
        "ac" | "mains" | "plugged" | "default" => Some(Mode::Ac),
        "always" | "on" | "yes" | "1" | "true" | "battery" => Some(Mode::Always),
        _ => None,
    }
}

/// `AGENTLINK_KEEP_AWAKE`, else `~/.agentlink/keep_awake`, else on AC power only.
pub fn mode() -> Mode {
    if let Ok(v) = std::env::var("AGENTLINK_KEEP_AWAKE") {
        return parse_mode(&v).unwrap_or(Mode::Ac);
    }
    std::fs::read_to_string(crate::paths::home().join("keep_awake"))
        .ok()
        .and_then(|t| parse_mode(&t))
        .unwrap_or(Mode::Ac)
}

/// How long a session may stay "working" before it stops counting: a session whose agent died without a
/// last hook must not hold the machine awake for ever.
pub const MAX_BUSY: Duration = Duration::from_secs(12 * 3600);

/// A session that is working or waiting on the user (a job that runs is `Work`).
pub fn busy(sessions: &[Session], now: u64) -> bool {
    sessions.iter().any(|s| {
        matches!(s.st, Some(SessState::Work | SessState::Input))
            && now.saturating_sub(s.tst.unwrap_or(now)) < MAX_BUSY.as_secs()
    })
}

/// The rule: hold the inhibitor when there is work and the mode and power source allow it. Unknown power
/// counts as AC (do not fail to protect a long job on a desktop).
pub fn wanted(mode: Mode, busy: bool, on_ac: Option<bool>) -> bool {
    busy && match mode {
        Mode::Off => false,
        Mode::Always => true,
        Mode::Ac => on_ac.unwrap_or(true),
    }
}

// ---- the inhibitor ----

pub trait Inhibitor: Send {
    /// Starts holding. `Err` carries a short reason (no tool, refused).
    fn take(&mut self) -> Result<(), String>;
    /// Stops holding; harmless when not held.
    fn release(&mut self);
    /// Still holding (a child that died is not).
    fn held(&mut self) -> bool;
    /// What holds it, for status.
    fn how(&self) -> &'static str;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum State {
    /// Not wanted now.
    Idle,
    Held,
    /// Wanted, but it could not be taken (the reason); retried.
    Failed(String),
    /// This system has no inhibitor.
    Unsupported,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Status {
    pub mode: Mode,
    pub state: State,
    pub busy: bool,
    pub on_ac: Option<bool>,
    pub how: &'static str,
}

impl Status {
    pub fn json(&self) -> Value {
        let (state, detail) = match &self.state {
            State::Idle => ("idle", None),
            State::Held => ("held", None),
            State::Failed(e) => ("failed", Some(e.clone())),
            State::Unsupported => ("unsupported", None),
        };
        json!({
            "mode": self.mode.name(),
            "state": state,
            "detail": detail,
            "busy": self.busy,
            "on_ac": self.on_ac,
            "how": self.how,
        })
    }
}

pub struct KeepAwake {
    inh: Box<dyn Inhibitor>,
    last: Status,
}

impl KeepAwake {
    pub fn new(inh: Box<dyn Inhibitor>) -> Self {
        let how = inh.how();
        KeepAwake {
            inh,
            last: Status {
                mode: Mode::Ac,
                state: if how == "unsupported" {
                    State::Unsupported
                } else {
                    State::Idle
                },
                busy: false,
                on_ac: None,
                how,
            },
        }
    }

    /// Brings the inhibitor in line with the rule. Called on every change and on a slow tick.
    pub fn update(&mut self, mode: Mode, busy: bool, on_ac: Option<bool>) -> Status {
        let how = self.inh.how();
        let want = wanted(mode, busy, on_ac);
        let state = if how == "unsupported" {
            State::Unsupported
        } else if want {
            if self.inh.held() {
                State::Held
            } else {
                match self.inh.take() {
                    Ok(()) => State::Held,
                    Err(e) => State::Failed(e),
                }
            }
        } else {
            if self.inh.held() {
                self.inh.release();
            }
            State::Idle
        };
        self.last = Status {
            mode,
            state,
            busy,
            on_ac,
            how,
        };
        self.last.clone()
    }

    pub fn release(&mut self) {
        self.inh.release();
        self.last.state = State::Idle;
    }
}

impl Drop for KeepAwake {
    fn drop(&mut self) {
        self.inh.release();
    }
}

/// A child process that holds the lock while it lives.
pub struct ChildInhibitor {
    cmd: Option<Vec<String>>,
    how: &'static str,
    child: Option<Child>,
}

impl ChildInhibitor {
    fn new(how: &'static str, cmd: Option<Vec<String>>) -> Self {
        ChildInhibitor {
            cmd,
            how,
            child: None,
        }
    }

    /// The system's inhibitor: a logind `idle` lock on Linux, `caffeinate -i` on macOS.
    pub fn system() -> Self {
        #[cfg(target_os = "linux")]
        {
            ChildInhibitor::new("systemd-inhibit", linux_command())
        }
        #[cfg(target_os = "macos")]
        {
            let pid = std::process::id().to_string();
            ChildInhibitor::new(
                "caffeinate",
                Some(vec!["caffeinate".into(), "-i".into(), "-w".into(), pid]),
            )
        }
        #[cfg(not(any(target_os = "linux", target_os = "macos")))]
        {
            ChildInhibitor::new("unsupported", None)
        }
    }
}

/// The command that holds the lock: `systemd-inhibit --what=idle ... sh -c 'while kill -0 PID; do sleep 2'`,
/// which ends when the daemon (PID) is gone.
#[cfg(target_os = "linux")]
fn linux_command() -> Option<Vec<String>> {
    let have = |tool: &str| {
        std::env::var_os("PATH")
            .into_iter()
            .flat_map(|p| std::env::split_paths(&p).collect::<Vec<_>>())
            .any(|d| d.join(tool).is_file())
    };
    if !have("systemd-inhibit") {
        return None;
    }
    let holder = [
        "sh".to_owned(),
        "-c".to_owned(),
        format!(
            "while kill -0 {} 2>/dev/null; do sleep 2; done",
            std::process::id()
        ),
    ];
    let mut v: Vec<String> = ["systemd-inhibit", "--what=idle", "--mode=block"]
        .iter()
        .map(|s| s.to_string())
        .collect();
    v.push("--who=agentlink".into());
    v.push("--why=an agent or job is running".into());
    v.extend(holder);
    Some(v)
}

impl Inhibitor for ChildInhibitor {
    fn take(&mut self) -> Result<(), String> {
        self.release();
        let Some(cmd) = &self.cmd else {
            return Err("no inhibitor on this system".into());
        };
        let mut c = Command::new(&cmd[0]);
        c.args(&cmd[1..])
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        #[cfg(unix)]
        {
            use std::os::unix::process::CommandExt;
            c.process_group(0); // its own group, so a release ends every process of the wrapper
        }
        let child = c.spawn().map_err(|e| format!("{}: {}", cmd[0], e.kind()))?;
        self.child = Some(child);
        // a tool that refuses (no logind, no permission) exits at once
        std::thread::sleep(Duration::from_millis(150));
        if self.held() {
            Ok(())
        } else {
            Err(format!("{} exited", self.how))
        }
    }

    fn release(&mut self) {
        if let Some(mut c) = self.child.take() {
            #[cfg(unix)]
            let group = i32::try_from(c.id()).ok();
            #[cfg(unix)]
            if let Some(g) = group {
                // SAFETY: kill(2) on the process group this struct created.
                unsafe { libc::kill(-g, libc::SIGTERM) };
            }
            for _ in 0..30 {
                if matches!(c.try_wait(), Ok(Some(_))) {
                    break;
                }
                std::thread::sleep(Duration::from_millis(10));
            }
            #[cfg(unix)]
            if let Some(g) = group {
                // whatever ignored the polite signal
                unsafe { libc::kill(-g, libc::SIGKILL) };
            }
            let _ = c.kill();
            let _ = c.wait();
        }
    }

    fn held(&mut self) -> bool {
        match self.child.as_mut().map(Child::try_wait) {
            Some(Ok(None)) => true,
            Some(_) => {
                self.child = None;
                false
            }
            None => false,
        }
    }

    fn how(&self) -> &'static str {
        if self.cmd.is_some() {
            self.how
        } else {
            "unsupported"
        }
    }
}

impl Drop for ChildInhibitor {
    fn drop(&mut self) {
        self.release();
    }
}

// ---- power source ----

/// Whether the machine runs on mains power, from `/sys/class/power_supply` entries `(type, online)`.
/// No battery (a desktop) counts as on AC.
pub fn ac_from_supplies(supplies: &[(String, Option<bool>)]) -> Option<bool> {
    if supplies.is_empty() {
        return None;
    }
    let has_battery = supplies.iter().any(|(t, _)| t == "Battery");
    let mains_online = supplies.iter().any(|(t, o)| {
        matches!(t.as_str(), "Mains" | "USB" | "USB_PD" | "USB_C") && *o == Some(true)
    });
    if mains_online || !has_battery {
        Some(true)
    } else {
        Some(false)
    }
}

/// `Now drawing from 'AC Power'` / `'Battery Power'` in `pmset -g ps`.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
pub fn parse_pmset(out: &str) -> Option<bool> {
    if out.contains("'AC Power'") {
        Some(true)
    } else if out.contains("'Battery Power'") || out.contains("'UPS Power'") {
        Some(false)
    } else {
        None
    }
}

pub fn on_ac() -> Option<bool> {
    match crate::presence::override_value("power").as_deref() {
        Some("ac") => return Some(true),
        Some("battery") => return Some(false),
        _ => {}
    }
    #[cfg(target_os = "linux")]
    {
        let mut v = Vec::new();
        for e in std::fs::read_dir("/sys/class/power_supply").ok()?.flatten() {
            let rd = |f: &str| std::fs::read_to_string(e.path().join(f)).ok();
            let Some(t) = rd("type") else { continue };
            let online = rd("online").map(|o| o.trim() == "1");
            v.push((t.trim().to_owned(), online));
        }
        ac_from_supplies(&v)
    }
    #[cfg(target_os = "macos")]
    {
        let o = crate::focus::run("pmset", &crate::focus::args(["-g", "ps"])).ok()?;
        parse_pmset(&o.stdout)
    }
    #[cfg(not(any(target_os = "linux", target_os = "macos")))]
    {
        None
    }
}

// ---- the daemon's task ----

static STATUS: OnceLock<Mutex<Option<Status>>> = OnceLock::new();

fn slot() -> &'static Mutex<Option<Status>> {
    STATUS.get_or_init(|| Mutex::new(None))
}

/// For `agentlink status`: the last state the task computed.
pub fn status_json() -> Value {
    match slot().lock().unwrap().as_ref() {
        Some(s) => s.json(),
        None => {
            json!({"mode": mode().name(), "state": "idle", "busy": false, "on_ac": null, "how": null})
        }
    }
}

/// How often the rule is re-checked without any change (power source, settings file).
const TICK: Duration = Duration::from_secs(5);

/// Runs the rule on every registry update and on a slow tick. The returned handle releases the
/// inhibitor when the task is stopped or aborted by [`Handle::shutdown`].
pub fn spawn(registry: Arc<Registry>, inhibitor: Box<dyn Inhibitor>) -> Handle {
    let ka = Arc::new(Mutex::new(KeepAwake::new(inhibitor)));
    let ka2 = ka.clone();
    let (_, mut rx) = registry.subscribe();
    let task = tokio::spawn(async move {
        loop {
            let sessions = registry.snapshot();
            let ka = ka2.clone();
            let st = tokio::task::spawn_blocking(move || {
                let st = ka
                    .lock()
                    .unwrap()
                    .update(mode(), busy(&sessions, now_unix()), on_ac());
                *slot().lock().unwrap() = Some(st.clone());
                st
            })
            .await;
            if let Ok(st) = &st {
                tracing::debug!("keep-awake {:?}", st.state);
            }
            tokio::select! {
                _ = tokio::time::sleep(TICK) => {}
                u = rx.recv() => {
                    if matches!(u, Err(tokio::sync::broadcast::error::RecvError::Closed)) {
                        return;
                    }
                    // let a burst of updates settle into one pass
                    tokio::time::sleep(Duration::from_millis(100)).await;
                    while rx.try_recv().is_ok() {}
                }
            }
        }
    });
    Handle { task, ka }
}

pub struct Handle {
    task: tokio::task::JoinHandle<()>,
    ka: Arc<Mutex<KeepAwake>>,
}

impl Handle {
    /// Stops the task and releases the inhibitor now.
    pub fn shutdown(self) {
        self.task.abort();
        self.ka.lock().unwrap().release();
        *slot().lock().unwrap() = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering::SeqCst};

    #[derive(Default, Clone)]
    struct Fake {
        takes: Arc<AtomicUsize>,
        releases: Arc<AtomicUsize>,
        held: Arc<Mutex<bool>>,
        fail: Arc<Mutex<Option<String>>>,
        unsupported: bool,
    }
    impl Inhibitor for Fake {
        fn take(&mut self) -> Result<(), String> {
            if let Some(e) = self.fail.lock().unwrap().clone() {
                return Err(e);
            }
            self.takes.fetch_add(1, SeqCst);
            *self.held.lock().unwrap() = true;
            Ok(())
        }
        fn release(&mut self) {
            if std::mem::replace(&mut *self.held.lock().unwrap(), false) {
                self.releases.fetch_add(1, SeqCst);
            }
        }
        fn held(&mut self) -> bool {
            *self.held.lock().unwrap()
        }
        fn how(&self) -> &'static str {
            if self.unsupported {
                "unsupported"
            } else {
                "fake"
            }
        }
    }

    fn sess(st: SessState, tst: u64) -> Session {
        let mut s = Session::delta(1);
        s.st = Some(st);
        s.tst = Some(tst);
        s
    }

    #[test]
    fn modes_parse() {
        assert_eq!(parse_mode("off"), Some(Mode::Off));
        assert_eq!(parse_mode("# c\nAC\n"), Some(Mode::Ac));
        assert_eq!(parse_mode("always # x"), Some(Mode::Always));
        assert_eq!(parse_mode("sometimes"), None);
        assert_eq!(parse_mode(""), None);
    }

    #[test]
    fn busy_means_working_or_waiting_and_not_stale() {
        let now = 1_000_000;
        assert!(busy(&[sess(SessState::Work, now)], now));
        assert!(busy(&[sess(SessState::Input, now - 100)], now));
        for st in [
            SessState::Start,
            SessState::Done,
            SessState::Error,
            SessState::Idle,
            SessState::Ended,
        ] {
            assert!(!busy(&[sess(st, now)], now), "{st:?}");
        }
        assert!(!busy(&[], now));
        assert!(!busy(
            &[sess(SessState::Work, now - MAX_BUSY.as_secs() - 1)],
            now
        ));
        assert!(busy(
            &[sess(SessState::Done, now), sess(SessState::Work, now)],
            now
        ));
    }

    #[test]
    fn the_rule_every_cell() {
        for (mode, busy, ac, want) in [
            (Mode::Off, true, Some(true), false),
            (Mode::Off, true, Some(false), false),
            (Mode::Ac, true, Some(true), true),
            (Mode::Ac, true, Some(false), false),
            (Mode::Ac, true, None, true),
            (Mode::Ac, false, Some(true), false),
            (Mode::Always, true, Some(false), true),
            (Mode::Always, true, None, true),
            (Mode::Always, false, Some(true), false),
        ] {
            assert_eq!(wanted(mode, busy, ac), want, "{mode:?} {busy} {ac:?}");
        }
    }

    #[test]
    fn life_cycle_take_hold_release() {
        let f = Fake::default();
        let mut ka = KeepAwake::new(Box::new(f.clone()));
        assert_eq!(ka.update(Mode::Ac, false, Some(true)).state, State::Idle);
        assert_eq!(f.takes.load(SeqCst), 0);
        assert_eq!(ka.update(Mode::Ac, true, Some(true)).state, State::Held);
        assert_eq!(ka.update(Mode::Ac, true, Some(true)).state, State::Held);
        assert_eq!(f.takes.load(SeqCst), 1, "taken once, not per update");
        // unplugged: released
        assert_eq!(ka.update(Mode::Ac, true, Some(false)).state, State::Idle);
        assert_eq!(f.releases.load(SeqCst), 1);
        // plugged in again: taken again
        assert_eq!(ka.update(Mode::Ac, true, Some(true)).state, State::Held);
        assert_eq!(f.takes.load(SeqCst), 2);
        // the setting switched off: released
        assert_eq!(ka.update(Mode::Off, true, Some(true)).state, State::Idle);
        assert_eq!(
            ka.update(Mode::Always, true, Some(false)).state,
            State::Held
        );
        // nothing runs: released
        assert_eq!(ka.update(Mode::Always, false, None).state, State::Idle);
        assert_eq!(f.takes.load(SeqCst), f.releases.load(SeqCst));
        assert!(!*f.held.lock().unwrap());
    }

    #[test]
    fn dropping_releases_and_shutdown_releases() {
        let f = Fake::default();
        {
            let mut ka = KeepAwake::new(Box::new(f.clone()));
            ka.update(Mode::Always, true, None);
            assert!(*f.held.lock().unwrap());
        }
        assert!(!*f.held.lock().unwrap(), "dropped: released");
        let mut ka = KeepAwake::new(Box::new(f.clone()));
        ka.update(Mode::Always, true, None);
        ka.release();
        assert!(!*f.held.lock().unwrap());
    }

    #[test]
    fn a_refusal_is_reported_and_retried() {
        let f = Fake::default();
        *f.fail.lock().unwrap() = Some("denied".into());
        let mut ka = KeepAwake::new(Box::new(f.clone()));
        assert_eq!(
            ka.update(Mode::Always, true, None).state,
            State::Failed("denied".into())
        );
        *f.fail.lock().unwrap() = None;
        assert_eq!(ka.update(Mode::Always, true, None).state, State::Held);
    }

    #[test]
    fn an_inhibitor_that_died_is_taken_again() {
        let f = Fake::default();
        let mut ka = KeepAwake::new(Box::new(f.clone()));
        ka.update(Mode::Always, true, None);
        *f.held.lock().unwrap() = false; // the child ended on its own
        assert_eq!(ka.update(Mode::Always, true, None).state, State::Held);
        assert_eq!(f.takes.load(SeqCst), 2);
    }

    #[test]
    fn no_inhibitor_is_reported_as_unsupported() {
        let f = Fake {
            unsupported: true,
            ..Default::default()
        };
        let mut ka = KeepAwake::new(Box::new(f.clone()));
        assert_eq!(
            ka.update(Mode::Always, true, None).state,
            State::Unsupported
        );
        assert_eq!(f.takes.load(SeqCst), 0);
        assert_eq!(
            ka.update(Mode::Always, true, None).json()["state"],
            "unsupported"
        );
    }

    #[test]
    fn power_sources() {
        let s = |t: &str, o: Option<bool>| (t.to_owned(), o);
        assert_eq!(ac_from_supplies(&[]), None);
        assert_eq!(
            ac_from_supplies(&[s("Mains", Some(true)), s("Battery", None)]),
            Some(true)
        );
        assert_eq!(
            ac_from_supplies(&[s("Mains", Some(false)), s("Battery", None)]),
            Some(false)
        );
        assert_eq!(
            ac_from_supplies(&[s("Mains", Some(false))]),
            Some(true),
            "no battery"
        );
        assert_eq!(parse_pmset("Now drawing from 'AC Power'\n"), Some(true));
        assert_eq!(
            parse_pmset("Now drawing from 'Battery Power'\n"),
            Some(false)
        );
        assert_eq!(parse_pmset(""), None);
    }

    #[cfg(unix)]
    #[test]
    fn a_child_inhibitor_ends_with_its_pipe_and_leaks_nothing() {
        let mut c = ChildInhibitor::new(
            "test",
            Some(vec!["sh".into(), "-c".into(), "sleep 30 & wait".into()]),
        );
        c.take().unwrap();
        let pid = c.child.as_ref().unwrap().id();
        assert!(c.held());
        c.release();
        assert!(!c.held());
        // reaped: its pid no longer names a process of ours
        assert!(
            !std::path::Path::new(&format!("/proc/{pid}")).exists()
                || cfg!(not(target_os = "linux"))
        );
        // one that refuses at once is an error, not a hold
        let mut bad = ChildInhibitor::new("test", Some(vec!["false".into()]));
        assert!(bad.take().is_err());
        let mut none = ChildInhibitor::new("test", None);
        assert_eq!(none.how(), "unsupported");
        assert!(none.take().is_err());
    }
}
