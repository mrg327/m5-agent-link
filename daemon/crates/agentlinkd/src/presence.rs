//! Presence (docs/design/PRESENCE.md): is the user at the keyboard, and is the terminal of a session the
//! window they are looking at? The answer changes only two things: how long a permission request holds
//! the terminal's own prompt back ([`crate::window::answer_window`]) and whether the watch buzzes. It
//! never answers a request, never hides a card and never changes what can be approved.
//!
//! One table, [`cell`], holds every rule. Unknown presence is [`Here::Away`]: alerts on, full window,
//! exactly as before presence existed.
//!
//! Sources, none of them a new dependency (small tools run without a shell and under a timeout):
//! Linux: the desktop's screen saver / idle monitor on the session bus (KDE and the freedesktop
//! `ScreenSaver`, GNOME's Mutter idle monitor and `org.gnome.ScreenSaver`), logind's `LockedHint` /
//! `IdleHint`; the focused window from kitty's remote control, else X11 (`xdotool`). macOS: the HID idle
//! time and the console lock from `ioreg`. Elsewhere unknown.
//!
//! **Test only:** `AGENTLINK_PRESENCE_FILE` names a file the daemon reads on every question instead of
//! asking the system, e.g. `state=present focus=yes power=battery`. Not for use outside tests.

#![cfg_attr(test, allow(dead_code))] // the system probes are not built into the unit tests

use crate::focus::{args, run};
use crate::registry::HostCtx;
use serde_json::{json, Value};
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

/// What the system says about the user.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    /// Input within the idle limit and the session not locked.
    Present,
    /// No input for longer than the idle limit.
    Away,
    /// The screen is locked.
    Locked,
    Unknown,
}

impl State {
    pub fn name(self) -> &'static str {
        match self {
            State::Present => "present",
            State::Away => "away",
            State::Locked => "locked",
            State::Unknown => "unknown",
        }
    }
}

/// Whether one session's terminal is the focused window.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Focus {
    Yes,
    No,
    Unknown,
}

impl Focus {
    pub fn name(self) -> &'static str {
        match self {
            Focus::Yes => "yes",
            Focus::No => "no",
            Focus::Unknown => "unknown",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Reading {
    pub state: State,
    /// Which source decided (`kde-screensaver`, `logind`, `override-file`, ...).
    pub source: &'static str,
    /// Time since the last input, when the source tells it.
    pub idle_ms: Option<u64>,
}

impl Reading {
    fn unknown() -> Self {
        Reading {
            state: State::Unknown,
            source: "none",
            idle_ms: None,
        }
    }
}

/// Where the user is, as far as the rules care.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Here {
    /// At the keyboard with this session's terminal focused: they can see it.
    Focused,
    /// At the keyboard, but not looking at that terminal (or the daemon cannot tell).
    AtKeyboard,
    /// Away, locked, unknown, or presence switched off: the behaviour from before presence existed.
    #[default]
    Away,
}

impl Here {
    pub fn name(self) -> &'static str {
        match self {
            Here::Focused => "focused",
            Here::AtKeyboard => "at_keyboard",
            Here::Away => "away",
        }
    }
}

/// What is being decided.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Event {
    /// How long a permission request holds the terminal's own prompt back.
    Window,
    /// A session or job finished (done, error, job ended).
    Finished,
    /// A request needs the user: an approval or a question (the card is always shown).
    Needs,
}

/// One cell of the table.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Cell {
    /// The full configured window.
    Full,
    /// A window of at most this long.
    Short(Duration),
    /// Buzz as usual.
    Buzz,
    /// No buzz, glow or nag. The card is shown all the same.
    Hush,
}

/// With the terminal in front of them the prompt should come at once; the watch still gets the card for
/// this long, so a card is never hidden.
pub const FOCUSED_WINDOW: Duration = Duration::from_secs(2);
/// At the keyboard elsewhere: long enough to notice the watch, short enough not to hold the terminal.
pub const KEYBOARD_WINDOW: Duration = Duration::from_secs(10);

/// THE TABLE (PRESENCE.md section 2). Nothing else in the daemon decides these.
pub fn cell(event: Event, here: Here) -> Cell {
    match (event, here) {
        (Event::Window, Here::Focused) => Cell::Short(FOCUSED_WINDOW),
        (Event::Window, Here::AtKeyboard) => Cell::Short(KEYBOARD_WINDOW),
        (Event::Window, Here::Away) => Cell::Full,
        (Event::Finished, Here::Focused) => Cell::Hush,
        (Event::Finished, Here::AtKeyboard) => Cell::Buzz,
        (Event::Finished, Here::Away) => Cell::Buzz,
        (Event::Needs, Here::Focused) => Cell::Hush,
        (Event::Needs, Here::AtKeyboard) => Cell::Buzz,
        (Event::Needs, Here::Away) => Cell::Buzz,
    }
}

/// Whether the watch should be told to stay quiet for `event` at `here`.
pub fn hush(event: Event, here: Here) -> bool {
    cell(event, here) == Cell::Hush
}

/// Combines the two readings.
pub fn here_from(user: State, focus: Focus) -> Here {
    match (user, focus) {
        (State::Present, Focus::Yes) => Here::Focused,
        (State::Present, _) => Here::AtKeyboard,
        _ => Here::Away,
    }
}

// ---- setting ----

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Settings {
    pub enabled: bool,
    /// Input newer than this counts as being at the keyboard.
    pub idle_limit: Duration,
}

pub const DEFAULT_IDLE: Duration = Duration::from_secs(120);

impl Default for Settings {
    fn default() -> Self {
        Settings {
            enabled: true,
            idle_limit: DEFAULT_IDLE,
        }
    }
}

/// `off` / `on`, optionally followed by the idle limit in seconds (`on 300`, `on idle=300`). The first
/// line that is not blank or a `#` comment counts.
pub fn parse_settings(text: &str) -> Settings {
    let mut s = Settings::default();
    let Some(line) = text
        .lines()
        .map(str::trim)
        .find(|l| !l.is_empty() && !l.starts_with('#'))
    else {
        return s;
    };
    for (i, w) in line.split_whitespace().enumerate() {
        match w.to_ascii_lowercase().as_str() {
            "off" | "0" | "false" | "no" if i == 0 => s.enabled = false,
            "on" | "1" | "true" | "yes" if i == 0 => s.enabled = true,
            w => {
                let n = w.strip_prefix("idle=").unwrap_or(w);
                if let Ok(secs) = n.parse::<u64>() {
                    s.idle_limit = Duration::from_secs(secs.clamp(10, 3600));
                }
            }
        }
    }
    s
}

/// `AGENTLINK_PRESENCE`, else `~/.agentlink/presence`; read on every question, so an edit applies at once.
pub fn settings() -> Settings {
    if let Ok(v) = std::env::var("AGENTLINK_PRESENCE") {
        return parse_settings(&v);
    }
    std::fs::read_to_string(crate::paths::home().join("presence"))
        .map(|t| parse_settings(&t))
        .unwrap_or_default()
}

// ---- probing ----

/// How the system is asked. Faked in tests.
pub trait Probe: Send + Sync {
    fn user(&self, idle_limit: Duration) -> Reading;
    /// Whether the terminal of this session is the focused window, and how that was found out.
    fn focus(&self, ctx: &HostCtx) -> (Focus, &'static str);
    /// How long a reading stays good.
    fn ttl(&self) -> Duration {
        Duration::from_secs(3)
    }
}

pub struct Presence {
    probe: Box<dyn Probe>,
    cache: Mutex<Option<(Instant, Duration, Reading)>>,
}

impl Presence {
    pub fn with_probe(probe: Box<dyn Probe>) -> Self {
        Presence {
            probe,
            cache: Mutex::new(None),
        }
    }

    /// The system probe, or the test override file when `AGENTLINK_PRESENCE_FILE` is set.
    pub fn system() -> Self {
        #[cfg(test)]
        {
            Self::with_probe(Box::new(Fixed(State::Unknown, Focus::Unknown)))
        }
        #[cfg(not(test))]
        {
            match std::env::var_os("AGENTLINK_PRESENCE_FILE") {
                Some(f) => Self::with_probe(Box::new(OverrideFile(f.into()))),
                None => Self::with_probe(Box::new(SystemProbe)),
            }
        }
    }

    /// The user reading (cached for the probe's `ttl`). Blocks while it asks the system.
    pub fn reading(&self) -> Reading {
        let set = settings();
        let mut c = self.cache.lock().unwrap();
        if let Some((at, limit, r)) = c.as_ref() {
            if at.elapsed() < self.probe.ttl() && *limit == set.idle_limit {
                return r.clone();
            }
        }
        let r = self.probe.user(set.idle_limit);
        *c = Some((Instant::now(), set.idle_limit, r.clone()));
        r
    }

    /// Where the user is relative to a session's terminal. Blocks (a few small processes at most).
    pub fn here(&self, ctx: Option<&HostCtx>) -> (Here, Reading, Focus) {
        let set = settings();
        if !set.enabled {
            return (Here::Away, Reading::unknown(), Focus::Unknown);
        }
        let r = self.reading();
        let focus = match (r.state, ctx) {
            (State::Present, Some(ctx)) => self.probe.focus(ctx).0,
            _ => Focus::Unknown,
        };
        (here_from(r.state, focus), r, focus)
    }

    /// [`Self::here`] off the async threads.
    pub async fn here_async(&'static self, ctx: Option<HostCtx>) -> (Here, Reading, Focus) {
        tokio::task::spawn_blocking(move || self.here(ctx.as_ref()))
            .await
            .unwrap_or((Here::Away, Reading::unknown(), Focus::Unknown))
    }

    /// For `agentlink status`: the setting, the reading and each given session's focus.
    pub fn status(&self, sessions: &[(u64, Option<HostCtx>)]) -> Value {
        let set = settings();
        let r = self.reading();
        let focus: Vec<Value> = sessions
            .iter()
            .map(|(sid, ctx)| {
                let (f, how) = match ctx {
                    Some(c) if set.enabled && r.state == State::Present => self.probe.focus(c),
                    _ => (Focus::Unknown, "none"),
                };
                json!({"sid": sid, "focus": f.name(), "source": how})
            })
            .collect();
        json!({
            "enabled": set.enabled,
            "state": if set.enabled { r.state.name() } else { "off" },
            "reading": r.state.name(),
            "source": r.source,
            "idle_s": r.idle_ms.map(|m| m / 1000),
            "idle_limit_s": set.idle_limit.as_secs(),
            "sessions": focus,
        })
    }
}

static GLOBAL: OnceLock<Presence> = OnceLock::new();

/// The daemon's one presence source.
pub fn global() -> &'static Presence {
    GLOBAL.get_or_init(Presence::system)
}

// ---- test override ----

/// Reads `key=value` words from the override file.
pub fn override_value(key: &str) -> Option<String> {
    let f = std::env::var_os("AGENTLINK_PRESENCE_FILE")?;
    let text = std::fs::read_to_string(f).ok()?;
    parse_override(&text, key)
}

pub fn parse_override(text: &str, key: &str) -> Option<String> {
    text.split_whitespace()
        .filter_map(|w| w.split_once('='))
        .find(|(k, _)| *k == key)
        .map(|(_, v)| v.to_owned())
}

#[allow(dead_code)]
struct OverrideFile(std::path::PathBuf);

impl Probe for OverrideFile {
    fn user(&self, _: Duration) -> Reading {
        let text = std::fs::read_to_string(&self.0).unwrap_or_default();
        let state = match parse_override(&text, "state").as_deref() {
            Some("present") => State::Present,
            Some("away") => State::Away,
            Some("locked") => State::Locked,
            _ => State::Unknown,
        };
        Reading {
            state,
            source: "override-file",
            idle_ms: None,
        }
    }
    fn focus(&self, _: &HostCtx) -> (Focus, &'static str) {
        let text = std::fs::read_to_string(&self.0).unwrap_or_default();
        let f = match parse_override(&text, "focus").as_deref() {
            Some("yes") => Focus::Yes,
            Some("no") => Focus::No,
            _ => Focus::Unknown,
        };
        (f, "override-file")
    }
    fn ttl(&self) -> Duration {
        Duration::ZERO
    }
}

#[cfg(test)]
pub struct Fixed(pub State, pub Focus);

#[cfg(test)]
impl Probe for Fixed {
    fn user(&self, _: Duration) -> Reading {
        Reading {
            state: self.0,
            source: "fixed",
            idle_ms: None,
        }
    }
    fn focus(&self, _: &HostCtx) -> (Focus, &'static str) {
        (self.1, "fixed")
    }
}

// ---- the system ----

#[allow(dead_code)]
struct SystemProbe;

impl Probe for SystemProbe {
    fn user(&self, idle_limit: Duration) -> Reading {
        #[cfg(target_os = "linux")]
        {
            linux_user(idle_limit)
        }
        #[cfg(target_os = "macos")]
        {
            macos_user(idle_limit)
        }
        #[cfg(not(any(target_os = "linux", target_os = "macos")))]
        {
            let _ = idle_limit;
            Reading::unknown()
        }
    }

    fn focus(&self, ctx: &HostCtx) -> (Focus, &'static str) {
        #[cfg(target_os = "linux")]
        {
            linux_focus(ctx)
        }
        #[cfg(not(target_os = "linux"))]
        {
            let _ = ctx;
            (Focus::Unknown, "none")
        }
    }
}

fn idle_state(idle_ms: u64, limit: Duration) -> State {
    if Duration::from_millis(idle_ms) <= limit {
        State::Present
    } else {
        State::Away
    }
}

/// The value of `busctl call` output: `b true` -> `true`, `u 9014759` -> `9014759`.
pub fn parse_busctl(out: &str) -> Option<&str> {
    let mut it = out.split_whitespace();
    let ty = it.next()?;
    let v = it.next()?;
    (ty.len() == 1).then_some(v)
}

/// `Key=value` lines of `loginctl show-session`.
pub fn parse_props<'a>(out: &'a str, key: &str) -> Option<&'a str> {
    out.lines()
        .filter_map(|l| l.split_once('='))
        .find(|(k, _)| *k == key)
        .map(|(_, v)| v.trim())
}

#[cfg(target_os = "linux")]
fn bus_call(dest: &str, path: &str, iface: &str, method: &str) -> Option<String> {
    let a = args(["--user", "--timeout=2", "call", dest, path, iface, method]);
    let o = run("busctl", &a).ok().filter(|o| o.ok)?;
    parse_busctl(&o.stdout).map(str::to_owned)
}

/// The logind session of this login: `$XDG_SESSION_ID`, else the user's graphical session.
#[cfg(target_os = "linux")]
fn session_id() -> Option<String> {
    if let Some(id) = std::env::var("XDG_SESSION_ID")
        .ok()
        .filter(|s| !s.is_empty() && s.chars().all(|c| c.is_ascii_alphanumeric()))
    {
        return Some(id);
    }
    use std::os::unix::fs::MetadataExt;
    let uid = std::fs::metadata("/proc/self").ok()?.uid().to_string();
    let o = run(
        "loginctl",
        &args(["show-user", &uid, "-p", "Display", "--value"]),
    )
    .ok()
    .filter(|o| o.ok)?;
    let id = o.stdout.trim().to_owned();
    (!id.is_empty() && id.chars().all(|c| c.is_ascii_alphanumeric())).then_some(id)
}

/// logind's `LockedHint` and `IdleHint` for the session.
#[cfg(target_os = "linux")]
fn logind() -> Option<(Option<bool>, Option<bool>)> {
    let id = session_id()?;
    let o = run(
        "loginctl",
        &args(["show-session", &id, "-p", "LockedHint", "-p", "IdleHint"]),
    )
    .ok()
    .filter(|o| o.ok)?;
    let yn = |k| match parse_props(&o.stdout, k) {
        Some("yes") => Some(true),
        Some("no") => Some(false),
        _ => None,
    };
    Some((yn("LockedHint"), yn("IdleHint")))
}

#[cfg(target_os = "linux")]
fn linux_user(limit: Duration) -> Reading {
    const FD: (&str, &str, &str) = (
        "org.freedesktop.ScreenSaver",
        "/ScreenSaver",
        "org.freedesktop.ScreenSaver",
    );
    // locked? the desktop first (it knows the moment the lock appears), then logind
    let mut hints = None;
    for (src, dest, path, iface) in [
        ("freedesktop-screensaver", FD.0, FD.1, FD.2),
        (
            "gnome-screensaver",
            "org.gnome.ScreenSaver",
            "/org/gnome/ScreenSaver",
            "org.gnome.ScreenSaver",
        ),
    ] {
        if bus_call(dest, path, iface, "GetActive").as_deref() == Some("true") {
            return locked(src);
        }
    }
    if let Some((l, i)) = logind() {
        if l == Some(true) {
            return locked("logind");
        }
        hints = i;
    }
    // idle time: KDE and others answer in ms; GNOME's Mutter too
    for (src, dest, path, iface, method) in [
        (
            "freedesktop-screensaver",
            FD.0,
            FD.1,
            FD.2,
            "GetSessionIdleTime",
        ),
        (
            "gnome-idle-monitor",
            "org.gnome.Mutter.IdleMonitor",
            "/org/gnome/Mutter/IdleMonitor/Core",
            "org.gnome.Mutter.IdleMonitor",
            "GetIdletime",
        ),
    ] {
        if let Some(ms) = bus_call(dest, path, iface, method).and_then(|v| v.parse::<u64>().ok()) {
            // a lock the desktop did not announce cannot be told apart from a quiet keyboard here;
            // the lock checks above ran first
            return Reading {
                state: idle_state(ms, limit),
                source: src,
                idle_ms: Some(ms),
            };
        }
    }
    // logind's idle hint is only trusted one way: "idle" means away, "not idle" proves nothing
    // (a desktop that does not set it leaves it at "no")
    if hints == Some(true) {
        return Reading {
            state: State::Away,
            source: "logind",
            idle_ms: None,
        };
    }
    Reading::unknown()
}

#[cfg(target_os = "linux")]
fn locked(source: &'static str) -> Reading {
    Reading {
        state: State::Locked,
        source,
        idle_ms: None,
    }
}

/// X11 window ids and pids from `xdotool` output are plain decimals.
fn decimal(s: &str) -> Option<u64> {
    let s = s.trim();
    (!s.is_empty() && s.len() <= 12 && s.chars().all(|c| c.is_ascii_digit()))
        .then(|| s.parse().ok())?
}

/// Whether the active window `(id, pid)` is the session's terminal: `WINDOWID` (set by X11 terminals),
/// kitty's pid, or one of the pids the hook captured.
pub fn is_terminal_window(ctx: &HostCtx, active_id: u64, active_pid: Option<u64>) -> bool {
    let env_u64 = |k: &str| ctx.env.get(k).and_then(|v| v.trim().parse::<u64>().ok());
    if env_u64("WINDOWID") == Some(active_id) {
        return true;
    }
    let Some(pid) = active_pid else {
        return false;
    };
    env_u64("KITTY_PID") == Some(pid) || ctx.pids.iter().any(|p| u64::from(*p) == pid)
}

#[cfg(target_os = "linux")]
fn linux_focus(ctx: &HostCtx) -> (Focus, &'static str) {
    // kitty knows its own focus (also on Wayland), down to the tab and split
    if let Some(f) = crate::focus::kitty_focused(ctx) {
        return (if f { Focus::Yes } else { Focus::No }, "kitty");
    }
    if std::env::var("XDG_SESSION_TYPE").as_deref() == Ok("wayland")
        || std::env::var_os("DISPLAY").is_none()
    {
        return (Focus::Unknown, "none");
    }
    let Some(id) = run("xdotool", &args(["getactivewindow"]))
        .ok()
        .filter(|o| o.ok)
        .and_then(|o| decimal(&o.stdout))
    else {
        return (Focus::Unknown, "none");
    };
    let pid = run("xdotool", &args(["getwindowpid", &id.to_string()]))
        .ok()
        .filter(|o| o.ok)
        .and_then(|o| decimal(&o.stdout));
    let f = if is_terminal_window(ctx, id, pid) {
        Focus::Yes
    } else {
        Focus::No
    };
    (f, "x11")
}

// ---- macOS ----

/// `"HIDIdleTime" = 4012345678` (nanoseconds) in `ioreg -c IOHIDSystem` output, as milliseconds.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
pub fn parse_hid_idle(out: &str) -> Option<u64> {
    let rest = out.split("\"HIDIdleTime\"").nth(1)?;
    let n: String = rest
        .trim_start_matches([' ', '=', '\t'])
        .chars()
        .take_while(char::is_ascii_digit)
        .collect();
    Some(n.parse::<u64>().ok()? / 1_000_000)
}

/// Whether `ioreg` shows the console session's screen as locked.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
pub fn parse_console_locked(out: &str) -> Option<bool> {
    let compact: String = out.split_whitespace().collect();
    if compact.contains("\"CGSSessionScreenIsLocked\"=Yes")
        || compact.contains("\"CGSSessionScreenIsLocked\"=1")
    {
        return Some(true);
    }
    compact.contains("\"IOConsoleUsers\"").then_some(false)
}

#[cfg(target_os = "macos")]
fn macos_user(limit: Duration) -> Reading {
    let lock = run("ioreg", &args(["-n", "Root", "-d1", "-l", "-w0"]))
        .ok()
        .filter(|o| o.ok)
        .and_then(|o| parse_console_locked(&o.stdout));
    if lock == Some(true) {
        return locked("ioreg-lock");
    }
    match run(
        "ioreg",
        &args(["-c", "IOHIDSystem", "-d", "4", "-r", "-w0"]),
    )
    .ok()
    .filter(|o| o.ok)
    .and_then(|o| parse_hid_idle(&o.stdout))
    {
        Some(ms) => Reading {
            state: idle_state(ms, limit),
            source: "hid-idle",
            idle_ms: Some(ms),
        },
        None => Reading::unknown(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const HERES: [Here; 3] = [Here::Focused, Here::AtKeyboard, Here::Away];

    #[test]
    fn the_table_every_cell() {
        use Cell::*;
        let want = [
            (
                Event::Window,
                [Short(FOCUSED_WINDOW), Short(KEYBOARD_WINDOW), Full],
            ),
            (Event::Finished, [Hush, Buzz, Buzz]),
            (Event::Needs, [Hush, Buzz, Buzz]),
        ];
        for (ev, row) in want {
            for (h, c) in HERES.iter().zip(row) {
                assert_eq!(cell(ev, *h), c, "{ev:?} {h:?}");
            }
        }
        assert!(hush(Event::Finished, Here::Focused));
        assert!(!hush(Event::Needs, Here::AtKeyboard));
        assert!(!hush(Event::Needs, Here::Away));
        // a hush is the only thing presence takes away, and only with the terminal in front of the user
        assert!(FOCUSED_WINDOW < KEYBOARD_WINDOW);
        assert!(!FOCUSED_WINDOW.is_zero(), "a card is never hidden");
    }

    #[test]
    fn user_and_focus_make_a_place() {
        use {Focus::*, State::*};
        for (u, f, h) in [
            (Present, Yes, Here::Focused),
            (Present, No, Here::AtKeyboard),
            (Present, Focus::Unknown, Here::AtKeyboard),
            (Away, Yes, Here::Away),
            (Locked, Yes, Here::Away),
            (State::Unknown, Yes, Here::Away),
            (State::Unknown, No, Here::Away),
        ] {
            assert_eq!(here_from(u, f), h, "{u:?} {f:?}");
        }
    }

    #[test]
    fn settings_parse() {
        let d = Settings::default();
        assert_eq!(parse_settings(""), d);
        assert_eq!(parse_settings("# nothing\n\n"), d);
        assert!(!parse_settings("off").enabled);
        assert!(!parse_settings("# x\n OFF # no\n").enabled);
        assert!(parse_settings("on").enabled);
        assert_eq!(
            parse_settings("on 300").idle_limit,
            Duration::from_secs(300)
        );
        assert_eq!(
            parse_settings("on idle=45").idle_limit,
            Duration::from_secs(45)
        );
        assert_eq!(parse_settings("on 1").idle_limit, Duration::from_secs(10));
        assert_eq!(parse_settings("on junk").idle_limit, DEFAULT_IDLE);
        let off = parse_settings("off\non");
        assert!(!off.enabled, "the first line counts");
    }

    #[test]
    fn busctl_and_logind_output() {
        assert_eq!(parse_busctl("u 9072895\n"), Some("9072895"));
        assert_eq!(parse_busctl("b true\n"), Some("true"));
        assert_eq!(parse_busctl("t 12\n"), Some("12"));
        assert_eq!(parse_busctl(""), None);
        assert_eq!(parse_busctl("Failed to call"), None);
        let out = "LockedHint=yes\nIdleHint=no\n";
        assert_eq!(parse_props(out, "LockedHint"), Some("yes"));
        assert_eq!(parse_props(out, "IdleHint"), Some("no"));
        assert_eq!(parse_props(out, "Nope"), None);
    }

    #[test]
    fn macos_output() {
        let io = "    | |   \"HIDIdleTime\" = 4012345678\n";
        assert_eq!(parse_hid_idle(io), Some(4012));
        assert_eq!(parse_hid_idle("nothing"), None);
        let locked = "\"IOConsoleUsers\" = ({\"CGSSessionScreenIsLocked\"=Yes,\"kCGSSessionOnConsoleKey\"=Yes})";
        assert_eq!(parse_console_locked(locked), Some(true));
        let open = "\"IOConsoleUsers\" = ({\"kCGSSessionOnConsoleKey\"=Yes})";
        assert_eq!(parse_console_locked(open), Some(false));
        assert_eq!(parse_console_locked("nothing"), None);
    }

    #[test]
    fn idle_limit_decides_present() {
        let lim = Duration::from_secs(120);
        assert_eq!(idle_state(0, lim), State::Present);
        assert_eq!(idle_state(120_000, lim), State::Present);
        assert_eq!(idle_state(120_001, lim), State::Away);
    }

    #[test]
    fn the_focused_window_is_the_sessions_terminal() {
        let mut ctx = HostCtx::default();
        ctx.env.insert("WINDOWID".into(), "60817422".into());
        ctx.pids = vec![100, 200];
        assert!(is_terminal_window(&ctx, 60817422, None));
        assert!(is_terminal_window(&ctx, 1, Some(200)));
        assert!(!is_terminal_window(&ctx, 1, Some(300)));
        assert!(!is_terminal_window(&ctx, 1, None));
        ctx.env.insert("KITTY_PID".into(), "999".into());
        assert!(is_terminal_window(&ctx, 1, Some(999)));
        assert_eq!(decimal("2045\n"), Some(2045));
        assert_eq!(decimal("0x1f"), None);
        assert_eq!(decimal(""), None);
    }

    #[test]
    fn override_file_words() {
        assert_eq!(
            parse_override("state=present focus=yes\n", "focus").as_deref(),
            Some("yes")
        );
        assert_eq!(parse_override("state=away", "focus"), None);
    }

    #[test]
    fn presence_combines_probe_setting_and_context() {
        let p = Presence::with_probe(Box::new(Fixed(State::Present, Focus::Yes)));
        let ctx = HostCtx::default();
        assert_eq!(p.here(Some(&ctx)).0, Here::Focused);
        // no context: the terminal cannot be found, so at the keyboard elsewhere
        assert_eq!(p.here(None).0, Here::AtKeyboard);
        for (u, h) in [
            (State::Away, Here::Away),
            (State::Locked, Here::Away),
            (State::Unknown, Here::Away),
        ] {
            let p = Presence::with_probe(Box::new(Fixed(u, Focus::Yes)));
            assert_eq!(p.here(Some(&ctx)).0, h, "{u:?}");
        }
        let st = p.status(&[(3, Some(ctx))]);
        assert_eq!(st["state"], "present");
        assert_eq!(st["sessions"][0]["focus"], "yes");
    }

    /// Manual: `cargo test -p agentlinkd live_presence -- --ignored --nocapture` asks this machine.
    #[test]
    #[ignore]
    fn live_presence() {
        let p = Presence::with_probe(Box::new(SystemProbe));
        println!("reading: {:?}", p.reading());
        let mut ctx = HostCtx::default();
        ctx.env.extend(
            [
                "WINDOWID",
                "KITTY_PID",
                "KITTY_WINDOW_ID",
                "KITTY_LISTEN_ON",
            ]
            .iter()
            .filter_map(|k| Some((k.to_string(), std::env::var(k).ok()?))),
        );
        ctx.pids = vec![std::env::var("AL_PID")
            .ok()
            .and_then(|p| p.parse().ok())
            .unwrap_or_else(std::process::id)];
        println!("focus: {:?}", SystemProbe.focus(&ctx));
        println!("here: {:?}", p.here(Some(&ctx)));
        println!("status: {}", p.status(&[(1, Some(ctx))]));
    }
}
