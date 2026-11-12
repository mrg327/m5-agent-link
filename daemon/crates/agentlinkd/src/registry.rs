//! Session registry: merges events from all `AgentSource`s into the device-visible
//! session list and broadcasts deltas.

use crate::trace::{Dir, Trace};
use agentlink_proto::{
    Agent, Attention, Clear, InputSub, PermMode, PrState, SessState, Session, Usage, CTX_UNKNOWN,
};
use serde::Serialize;
use std::collections::{BTreeMap, HashMap};
use std::path::PathBuf;
use std::sync::Mutex;
use tokio::sync::broadcast;

/// Snapshot limit from the CDDL (`[0*32 session]`).
const MAX_SESSIONS: usize = 32;

/// Longest snooze a device may ask for.
const MAX_SNOOZE_MIN: u64 = 24 * 60;

/// What the focus driver needs to find the agent's terminal.
#[derive(Debug, Clone, Default, Serialize)]
pub struct HostCtx {
    pub cwd: Option<String>,
    pub env: BTreeMap<String, String>,
    /// Controlling tty of the agent (`/dev/ttysNNN`), as captured by the hook.
    pub tty: Option<String>,
    /// Hook's parent pid chain, nearest first (agent process, its shell, the terminal, ...).
    pub pids: Vec<u32>,
}

/// Fields an `AgentSource` knows about; `None` = no information.
#[derive(Debug, Clone)]
pub struct SessionPatch {
    pub agent: Agent,
    pub state: Option<SessState>,
    pub sub: Option<InputSub>,
    pub repo: Option<String>,
    pub branch: Option<String>,
    pub tool: Option<String>,
    pub summary: Option<String>,
    pub step: Step,
    /// The closing question and its answers (ask.rs); ("", []) clears them when the agent moves on.
    pub ask: Option<(String, Vec<String>)>,
    /// A job's start (jobs.rs): the session's `t0`, which a hook-fed session sets itself at creation.
    pub t0: Option<u64>,
    /// A job's expected run time (0 = unknown) and exit status. Sent as given, even when equal
    /// to the last value: a reused session's new job must restate them.
    pub eta: Option<u64>,
    pub exit: Option<u64>,
    /// The agent's permission mode, from the hook payload (Claude Code's `permission_mode`; GAUGES.md).
    pub pm: Option<PermMode>,
}

/// What a hook line did to the turn's tool-step counter.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub enum Step {
    #[default]
    None,
    /// A tool starts: `n` + 1, result cleared.
    Start,
    /// The current tool ended (true = succeeded).
    Result(bool),
    /// A new prompt: `n` = 0.
    Reset,
}

impl SessionPatch {
    pub fn new(agent: Agent) -> Self {
        SessionPatch {
            agent,
            state: None,
            sub: None,
            repo: None,
            branch: None,
            tool: None,
            summary: None,
            ask: None,
            t0: None,
            eta: None,
            exit: None,
            pm: None,
            step: Step::None,
        }
    }
}

/// Where an event came from, as the source saw it. `host` is already sanitized.
#[derive(Debug, Clone, PartialEq)]
pub enum Via {
    Local,
    Remote {
        host: String,
        os: String,
        attach: Option<u64>,
    },
}

impl Via {
    /// Registry key for a source-level key. Remote keys can never equal a local one.
    pub fn key(&self, key: &str) -> String {
        match self {
            Via::Local => key.to_owned(),
            Via::Remote { host, .. } => format!("r:{}|{key}", host.to_lowercase()),
        }
    }
}

/// Where a recorded session lives. Only the daemon's own machine (or the local window that
/// holds the SSH connection) may ever be handed to a focus or scroll driver: see `route`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Origin {
    Local,
    Remote { host_idx: u64, attach: Option<u64> },
}

/// What a focus or scroll command may act on.
#[allow(clippy::large_enum_variant)]
pub enum Route {
    Unknown,
    /// A remote session without a live attach window.
    Unsupported,
    /// The session and the local context the drivers may use.
    Ready(Session, HostCtx),
}

#[derive(Debug)]
#[allow(clippy::large_enum_variant)] // short-lived, passed by value once
pub enum SessionEvent {
    /// `key` identifies the session within its source (e.g. `copilot:<sessionId>`).
    Update {
        key: String,
        via: Via,
        patch: SessionPatch,
        ctx: HostCtx,
    },
    End {
        key: String,
    },
}

/// Broadcast to every connected device link.
#[derive(Debug, Clone)]
pub enum Update {
    /// Full object for a new session, otherwise only the changed keys.
    Upsert(Session),
    Gone(u64),
    /// A request the devices may answer (from the approval broker).
    Attention(Attention),
    /// That request is over: answered (on either side), expired or cancelled.
    Clear(Clear),
    /// The plan's rate-limit windows changed (usage.rs; devices with proto >= 0.10).
    Usage(Usage),
}

struct Record {
    session: Session,
    /// Display only for remote sessions.
    ctx: HostCtx,
    origin: Origin,
}

struct RemoteHost {
    name: String,
    os: String,
}

struct Attach {
    socket: PathBuf,
    ctx: HostCtx,
}

#[derive(Default)]
struct State {
    next_sid: u64,
    by_key: HashMap<String, u64>,
    sessions: BTreeMap<u64, Record>,
    /// Host index = position + 1; 0 is the local machine.
    hosts: Vec<RemoteHost>,
    next_attach: u64,
    attaches: BTreeMap<u64, Attach>,
}

pub struct Registry {
    state: Mutex<State>,
    /// The account's usage windows (usage.rs), fed by local status lines.
    usage: Mutex<crate::usage::Tracker>,
    tx: broadcast::Sender<Update>,
    trace: Trace,
}

pub fn now_unix() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

pub fn clip(s: &str, max: usize) -> String {
    if s.chars().count() <= max {
        return s.to_owned();
    }
    let mut out: String = s.chars().take(max - 1).collect();
    out.push('…');
    out
}

/// Sets `*slot` when the value differs; returns whether it changed.
fn set<T: PartialEq + Clone>(slot: &mut Option<T>, new: Option<T>) -> bool {
    match new {
        Some(v) if slot.as_ref() != Some(&v) => {
            *slot = Some(v);
            true
        }
        _ => false,
    }
}

impl Registry {
    pub fn new(trace: Trace) -> Self {
        let (tx, _) = broadcast::channel(256);
        Registry {
            state: Mutex::new(State {
                next_sid: 1,
                next_attach: 1,
                ..Default::default()
            }),
            usage: Mutex::default(),
            tx,
            trace,
        }
    }

    /// A status-line run on this machine: records its rate-limit windows and publishes a `usage` body
    /// when the figures changed (usage.rs).
    pub fn record_usage(&self, rates: crate::usage::Rates) {
        let body = self.usage.lock().unwrap().record(rates, now_unix());
        if let Some(u) = body {
            self.publish(Update::Usage(u));
        }
    }

    /// The last published usage body, for a device that connects now.
    pub fn usage(&self) -> Option<Usage> {
        self.usage.lock().unwrap().last(now_unix())
    }

    /// A status-line run's readouts (context fill, model, pull request) for a session the hooks already
    /// created; an unknown key is ignored, so a status line alone never makes a card. Returns whether the
    /// session is known.
    pub fn readout(&self, key: &str, r: &crate::usage::Readout) -> bool {
        let upd = {
            let mut st = self.state.lock().unwrap();
            let Some(sid) = st.by_key.get(key).copied() else {
                return false;
            };
            let Some(rec) = st.sessions.get_mut(&sid) else {
                return false;
            };
            let s = &mut rec.session;
            let mut d = Session::delta(sid);
            let mut changed = false;
            // a context that was never known is not cleared: nothing to say yet
            if r.ctx.is_some()
                && !(s.ctx.is_none() && r.ctx == Some(CTX_UNKNOWN))
                && set(&mut s.ctx, r.ctx)
            {
                d.ctx = s.ctx;
                changed = true;
            }
            if set(&mut s.mdl, r.mdl.clone()) {
                d.mdl = s.mdl.clone();
                changed = true;
            }
            if let Some((n, state)) = r.pr {
                let was = (s.pr.unwrap_or(0), s.prs.unwrap_or(PrState::None));
                if was != (n, state) && !(s.pr.is_none() && n == 0) {
                    (s.pr, s.prs) = (Some(n), Some(state));
                    (d.pr, d.prs) = (s.pr, s.prs);
                    changed = true;
                }
            }
            changed.then_some(d)
        };
        if let Some(d) = upd {
            self.publish(Update::Upsert(d));
        }
        true
    }

    pub fn apply(&self, ev: SessionEvent) {
        let update = {
            let mut st = self.state.lock().unwrap();
            match ev {
                SessionEvent::Update {
                    key,
                    via,
                    patch,
                    ctx,
                } => Self::upsert(&mut st, key, via, patch, ctx),
                SessionEvent::End { key } => st
                    .by_key
                    .remove(&key)
                    .and_then(|sid| st.sessions.remove(&sid).map(|_| Update::Gone(sid))),
            }
        };
        if let Some(u) = update {
            self.publish(u);
        }
    }

    /// Traces and broadcasts one update to every device link.
    pub fn publish(&self, u: Update) {
        let (kind, sid, rid, msg) = match &u {
            Update::Upsert(s) => {
                let mut v = serde_json::to_value(s).unwrap_or_default();
                crate::trace::redact_sum(&mut v);
                ("session_upd", s.sid, None, v)
            }
            Update::Gone(sid) => ("gone", *sid, None, serde_json::json!({"sid": sid})),
            Update::Attention(a) => (
                "attention",
                a.sid,
                Some(a.rid),
                serde_json::to_value(a).unwrap_or_default(),
            ),
            Update::Clear(c) => (
                "clear",
                0,
                Some(c.rid),
                serde_json::to_value(c).unwrap_or_default(),
            ),
            Update::Usage(u) => (
                "usage",
                0,
                None,
                serde_json::to_value(u).unwrap_or_default(),
            ),
        };
        self.trace.record(
            "registry",
            Dir::Internal,
            kind,
            (sid != 0).then_some(sid),
            rid,
            msg,
            None,
        );
        let _ = self.tx.send(u); // no receivers is fine
    }

    /// The device-visible id of a source session key, once it has been seen.
    pub fn sid_of(&self, key: &str) -> Option<u64> {
        self.state.lock().unwrap().by_key.get(key).copied()
    }

    /// Host index of a remote name (case-insensitive), allocated on first sight.
    fn origin_of(st: &mut State, via: &Via) -> Origin {
        let Via::Remote { host, os, attach } = via else {
            return Origin::Local;
        };
        let pos = st
            .hosts
            .iter()
            .position(|h| h.name.eq_ignore_ascii_case(host))
            .unwrap_or_else(|| {
                st.hosts.push(RemoteHost {
                    name: host.clone(),
                    os: os.clone(),
                });
                st.hosts.len() - 1
            });
        Origin::Remote {
            host_idx: pos as u64 + 1,
            attach: *attach,
        }
    }

    fn upsert(
        st: &mut State,
        key: String,
        via: Via,
        p: SessionPatch,
        ctx: HostCtx,
    ) -> Option<Update> {
        let now = now_unix();
        let origin = Self::origin_of(st, &via);
        let created = !st.by_key.contains_key(&key);
        let sid = *st.by_key.entry(key).or_insert(st.next_sid);
        if created {
            st.next_sid += 1;
            let mut s = Session::delta(sid);
            s.ag = Some(p.agent);
            s.st = Some(SessState::Start);
            s.t0 = Some(now);
            s.tst = Some(now);
            s.seen = Some(false);
            if let (Origin::Remote { host_idx, .. }, Via::Remote { host, .. }) = (origin, &via) {
                s.host = Some(host_idx);
                s.hn = Some(clip(host, 16));
            } else {
                s.host = Some(0);
            }
            st.sessions.insert(
                sid,
                Record {
                    session: s,
                    ctx: HostCtx::default(),
                    origin,
                },
            );
        }
        let rec = st.sessions.get_mut(&sid)?;
        rec.origin = origin; // the latest attach a session's lines arrived on wins
        let old = std::mem::replace(&mut rec.ctx, ctx);
        // A hook that failed to capture identity must not erase what an earlier one found.
        if rec.ctx.tty.is_none() {
            rec.ctx.tty = old.tty;
        }
        if rec.ctx.pids.is_empty() {
            rec.ctx.pids = old.pids;
        }
        if rec.ctx.env.is_empty() {
            rec.ctx.env = old.env;
        }
        let s = &mut rec.session;
        let mut d = Session::delta(sid);
        let mut changed = created;
        macro_rules! upd {
            ($f:ident, $new:expr) => {
                if set(&mut s.$f, $new) {
                    d.$f = s.$f.clone();
                    changed = true;
                }
            };
        }
        upd!(st, p.state);
        if d.st.is_some() {
            s.tst = Some(now);
            d.tst = s.tst;
            if p.sub.is_none() {
                s.sub = None; // a delta cannot clear a key; devices only read `sub` while st = input
            }
        }
        upd!(sub, p.sub);
        upd!(repo, p.repo.map(|v| clip(&v, 32)));
        upd!(br, p.branch.map(|v| clip(&v, 32)));
        upd!(tool, p.tool);
        upd!(sum, p.summary.map(|v| clip(&v, 80)));
        if d.st == Some(SessState::Work) && p.exit.is_none() {
            s.exit = None; // a new run: the last one's status must not reach a new device
        }
        if let Some(t0) = p.t0 {
            (s.t0, d.t0, changed) = (Some(t0), Some(t0), true);
        }
        if let Some(eta) = p.eta {
            (s.eta, d.eta, changed) = (Some(eta), Some(eta), true);
        }
        upd!(exit, p.exit);
        upd!(pm, p.pm);
        if let Some((q, rp)) = p.ask {
            upd!(q, Some(q));
            upd!(rp, Some(rp));
        }
        match p.step {
            Step::None => {}
            Step::Start => {
                s.n = Some(s.n.unwrap_or(0) + 1);
                d.n = s.n;
                // absent = unchanged in a delta, so the device reads a new `n` as "running"
                s.ok = None;
                changed = true;
            }
            Step::Result(ok) if s.n.unwrap_or(0) > 0 => {
                if set(&mut s.ok, Some(ok)) {
                    d.ok = s.ok;
                    changed = true;
                }
            }
            Step::Result(_) => {}
            Step::Reset => {
                s.ok = None;
                if s.n.is_some_and(|n| n != 0) {
                    s.n = Some(0);
                    d.n = s.n;
                    changed = true;
                }
            }
        }
        if matches!(d.st, Some(SessState::Done | SessState::Error)) {
            s.seen = Some(false);
            d.seen = Some(false);
            if s.ntf == Some(true) {
                s.ntf = Some(false);
                d.ntf = Some(false);
            }
        }
        if !changed {
            return None;
        }
        Some(Update::Upsert(if created { s.clone() } else { d }))
    }

    /// Applies a device command to one session and broadcasts what changed (`f` fills the delta
    /// and returns whether anything did). `None` = unknown sid.
    fn edit(
        &self,
        sid: u64,
        f: impl FnOnce(&mut Session, &mut Session, u64) -> bool,
    ) -> Option<()> {
        let upd = {
            let mut st = self.state.lock().unwrap();
            let s = &mut st.sessions.get_mut(&sid)?.session;
            let mut d = Session::delta(sid);
            f(s, &mut d, now_unix()).then_some(d)
        };
        if let Some(d) = upd {
            self.publish(Update::Upsert(d));
        }
        Some(())
    }

    /// Device `ack`: the user has seen the session's outcome; an error then rests as idle.
    pub fn ack(&self, sid: u64) -> Option<()> {
        self.edit(sid, |s, d, now| {
            let mut changed = false;
            if s.seen != Some(true) {
                s.seen = Some(true);
                d.seen = Some(true);
                changed = true;
            }
            if s.st == Some(SessState::Error) {
                s.st = Some(SessState::Idle);
                s.tst = Some(now);
                d.st = s.st;
                d.tst = s.tst;
                s.sub = None;
                changed = true;
            }
            changed
        })
    }

    /// Device `snooze`: until now + `minutes` (0 clears it).
    pub fn snooze(&self, sid: u64, minutes: u64) -> Option<()> {
        self.edit(sid, |s, d, now| {
            let until = if minutes == 0 {
                0
            } else {
                now + minutes.min(MAX_SNOOZE_MIN) * 60
            };
            s.snz = Some(until);
            d.snz = s.snz;
            true
        })
    }

    /// Device `notify_done`: arm or disarm the one-shot "tell me when it finishes".
    pub fn notify_done(&self, sid: u64, arm: bool) -> Option<()> {
        self.edit(sid, |s, d, _| {
            if set(&mut s.ntf, Some(arm)) {
                d.ntf = s.ntf;
                return true;
            }
            false
        })
    }

    /// Current sessions (newest state change first, capped) and a receiver that sees every
    /// update applied after this snapshot.
    pub fn subscribe(&self) -> (Vec<Session>, broadcast::Receiver<Update>) {
        let st = self.state.lock().unwrap();
        (Self::list(&st), self.tx.subscribe())
    }

    pub fn snapshot(&self) -> Vec<Session> {
        Self::list(&self.state.lock().unwrap())
    }

    fn list(st: &State) -> Vec<Session> {
        let mut v: Vec<Session> = st.sessions.values().map(|r| r.session.clone()).collect();
        v.sort_by_key(|s| std::cmp::Reverse(s.tst));
        v.truncate(MAX_SESSIONS);
        v
    }

    /// The context a focus or scroll driver may use for `sid`. A remote session's own context
    /// describes another machine and is never returned.
    pub fn route(&self, sid: u64) -> Route {
        let st = self.state.lock().unwrap();
        let Some(rec) = st.sessions.get(&sid) else {
            return Route::Unknown;
        };
        let ctx = match rec.origin {
            Origin::Local => rec.ctx.clone(),
            Origin::Remote {
                attach: Some(id), ..
            } => match st.attaches.get(&id) {
                Some(a) => a.ctx.clone(),
                None => return Route::Unsupported,
            },
            Origin::Remote { attach: None, .. } => return Route::Unsupported,
        };
        Route::Ready(rec.session.clone(), ctx)
    }

    /// The terminal identity of a session on this machine (its own, or its attach window's), for
    /// presence. `None` for a session whose terminal is on another host with no attach window.
    pub fn host_ctx(&self, sid: u64) -> Option<HostCtx> {
        match self.route(sid) {
            Route::Ready(_, ctx) => Some(ctx),
            _ => None,
        }
    }

    /// The agent kind of a session.
    pub fn agent_of(&self, sid: u64) -> Option<Agent> {
        self.state.lock().unwrap().sessions.get(&sid)?.session.ag
    }

    /// Whether a local non-job session runs in the terminal with this tty: its own card covers
    /// whatever the shell runs there (jobs.rs).
    pub fn tty_hosts_agent(&self, tty: &str) -> bool {
        let st = self.state.lock().unwrap();
        st.sessions.values().any(|r| {
            r.origin == Origin::Local
                && r.session.ag != Some(Agent::Shell)
                && r.ctx.tty.as_deref() == Some(tty)
        })
    }

    /// Host index of a session (0 = local), for audit lines.
    pub fn host_of(&self, sid: u64) -> Option<u64> {
        self.state.lock().unwrap().sessions.get(&sid)?.session.host
    }

    pub fn new_attach_id(&self) -> u64 {
        let mut st = self.state.lock().unwrap();
        st.next_attach += 1;
        st.next_attach - 1
    }

    /// Sessions that arrive through `id` become routable until `drop_attach`.
    pub fn add_attach(&self, id: u64, socket: PathBuf, ctx: HostCtx) {
        self.state
            .lock()
            .unwrap()
            .attaches
            .insert(id, Attach { socket, ctx });
    }

    /// Closes an attach. Its sessions go too: once the tunnel is down their `sessionEnd` can
    /// never arrive, and a live one comes back with its next hook through a new attach.
    pub fn drop_attach(&self, id: u64) {
        let gone: Vec<u64> = {
            let mut st = self.state.lock().unwrap();
            st.attaches.remove(&id);
            let sids: Vec<u64> = st
                .sessions
                .iter()
                .filter(
                    |(_, r)| matches!(r.origin, Origin::Remote { attach: Some(a), .. } if a == id),
                )
                .map(|(sid, _)| *sid)
                .collect();
            for sid in &sids {
                st.sessions.remove(sid);
            }
            st.by_key.retain(|_, sid| !sids.contains(sid));
            sids
        };
        for sid in gone {
            self.publish(Update::Gone(sid));
        }
    }

    pub fn attach_sockets(&self) -> Vec<PathBuf> {
        let st = self.state.lock().unwrap();
        st.attaches.values().map(|a| a.socket.clone()).collect()
    }

    /// `{idx, name, os, sessions}` per remote host, for `agentlink status`.
    pub fn remotes(&self) -> Vec<serde_json::Value> {
        let st = self.state.lock().unwrap();
        st.hosts
            .iter()
            .enumerate()
            .map(|(i, h)| {
                let idx = i as u64 + 1;
                let n = st
                    .sessions
                    .values()
                    .filter(
                        |r| matches!(r.origin, Origin::Remote { host_idx, .. } if host_idx == idx),
                    )
                    .count();
                serde_json::json!({"idx": idx, "name": h.name, "os": h.os, "sessions": n})
            })
            .collect()
    }

    /// `{id, socket, window}` per live attach; `window` has only the terminal-identifying env.
    pub fn attaches(&self) -> Vec<serde_json::Value> {
        let st = self.state.lock().unwrap();
        st.attaches
            .iter()
            .map(|(id, a)| {
                let window: BTreeMap<_, _> = ["KITTY_WINDOW_ID", "TMUX_PANE", "TERM_PROGRAM"]
                    .into_iter()
                    .filter_map(|k| Some((k, a.ctx.env.get(k)?)))
                    .collect();
                serde_json::json!({"id": id, "socket": a.socket, "window": window})
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reg() -> Registry {
        Registry::new(Trace::null())
    }

    fn ev(state: SessState) -> SessionEvent {
        let mut p = SessionPatch::new(Agent::Copilot);
        p.state = Some(state);
        SessionEvent::Update {
            key: "k".into(),
            via: Via::Local,
            patch: p,
            ctx: HostCtx::default(),
        }
    }

    #[test]
    fn new_session_then_delta_then_gone() {
        let r = reg();
        let (_, mut rx) = r.subscribe();
        r.apply(ev(SessState::Work));
        let Update::Upsert(full) = rx.try_recv().unwrap() else {
            panic!()
        };
        assert_eq!(
            (full.sid, full.st, full.ag),
            (1, Some(SessState::Work), Some(Agent::Copilot))
        );
        r.apply(ev(SessState::Work));
        assert!(rx.try_recv().is_err(), "no change, no update");
        r.apply(ev(SessState::Done));
        let Update::Upsert(d) = rx.try_recv().unwrap() else {
            panic!()
        };
        assert_eq!((d.st, d.ag, d.t0), (Some(SessState::Done), None, None));
        r.apply(SessionEvent::End { key: "k".into() });
        assert!(matches!(rx.try_recv().unwrap(), Update::Gone(1)));
    }

    fn remote(host: &str, attach: Option<u64>) -> Via {
        Via::Remote {
            host: host.into(),
            os: "win".into(),
            attach,
        }
    }

    fn remote_ev(host: &str, attach: Option<u64>, key: &str, env: &[(&str, &str)]) -> SessionEvent {
        let via = remote(host, attach);
        let mut p = SessionPatch::new(Agent::Copilot);
        p.state = Some(SessState::Work);
        SessionEvent::Update {
            key: via.key(key),
            via,
            patch: p,
            ctx: HostCtx {
                env: env
                    .iter()
                    .map(|(k, v)| (k.to_string(), v.to_string()))
                    .collect(),
                ..Default::default()
            },
        }
    }

    fn step_ev(step: Step, state: SessState) -> SessionEvent {
        let SessionEvent::Update {
            key,
            via,
            mut patch,
            ctx,
        } = ev(state)
        else {
            unreachable!()
        };
        patch.step = step;
        SessionEvent::Update {
            key,
            via,
            patch,
            ctx,
        }
    }

    fn next_delta(rx: &mut broadcast::Receiver<Update>) -> Session {
        let Update::Upsert(d) = rx.try_recv().unwrap() else {
            panic!()
        };
        d
    }

    #[test]
    fn steps_count_ok_and_reset() {
        let r = reg();
        let (_, mut rx) = r.subscribe();
        r.apply(step_ev(Step::Result(true), SessState::Work));
        next_delta(&mut rx); // created
        assert_eq!(r.snapshot()[0].ok, None, "no step running: result ignored");
        r.apply(step_ev(Step::Start, SessState::Work));
        let d = next_delta(&mut rx);
        assert_eq!((d.n, d.ok), (Some(1), None));
        r.apply(step_ev(Step::Result(false), SessState::Work));
        let d = next_delta(&mut rx);
        assert_eq!((d.n, d.ok), (None, Some(false)));
        r.apply(step_ev(Step::Start, SessState::Work));
        let d = next_delta(&mut rx);
        assert_eq!((d.n, d.ok), (Some(2), None));
        assert_eq!(r.snapshot()[0].ok, None, "stored ok cleared while it runs");
        r.apply(step_ev(Step::Reset, SessState::Work));
        assert_eq!(next_delta(&mut rx).n, Some(0));
    }

    #[test]
    fn finishing_clears_seen_and_disarms_notify_ack_and_snooze() {
        let r = reg();
        let (_, mut rx) = r.subscribe();
        r.apply(ev(SessState::Work));
        next_delta(&mut rx);
        assert_eq!(r.ack(9), None);
        r.notify_done(1, true).unwrap();
        assert_eq!(next_delta(&mut rx).ntf, Some(true));
        r.apply(ev(SessState::Done));
        let d = next_delta(&mut rx);
        assert_eq!((d.seen, d.ntf), (Some(false), Some(false)));
        r.ack(1).unwrap();
        let d = next_delta(&mut rx);
        assert_eq!((d.seen, d.st), (Some(true), None), "done stays done");
        r.apply(ev(SessState::Work));
        next_delta(&mut rx);
        r.apply(ev(SessState::Error));
        assert_eq!(next_delta(&mut rx).seen, Some(false));
        r.ack(1).unwrap();
        let d = next_delta(&mut rx);
        assert_eq!((d.seen, d.st), (Some(true), Some(SessState::Idle)));
        r.snooze(1, 30).unwrap();
        let until = next_delta(&mut rx).snz.unwrap();
        assert!((until - now_unix() - 1800) <= 1);
        r.snooze(1, 99999).unwrap();
        assert!(next_delta(&mut rx).snz.unwrap() <= now_unix() + 86400);
        r.snooze(1, 0).unwrap();
        assert_eq!(next_delta(&mut rx).snz, Some(0));
    }

    #[test]
    fn remote_session_gets_host_index_name_and_its_own_key() {
        let r = reg();
        r.apply(ev(SessState::Work)); // local key "k"
        r.apply(remote_ev("WIN-BUILD01-LONGNAME-X", None, "k", &[]));
        r.apply(remote_ev("win-build01-longname-x", None, "k", &[]));
        r.apply(remote_ev("other", None, "k", &[]));
        let by_sid = |sid| r.snapshot().into_iter().find(|s| s.sid == sid).unwrap();
        let (local, rem, rem2) = (by_sid(1), by_sid(2), by_sid(3));
        assert_eq!((local.host, local.hn), (Some(0), None));
        assert_eq!(rem.host, Some(1));
        assert_eq!(rem.hn.as_deref(), Some("WIN-BUILD01-LON…"));
        assert_eq!(rem2.host, Some(2), "same name (any case) reuses the index");
        assert_eq!(
            r.snapshot().len(),
            3,
            "same session id on another host is distinct"
        );
        assert_eq!(r.remotes()[0]["sessions"], 1);
    }

    #[test]
    fn remote_focus_target_is_only_ever_the_live_attach_window() {
        let r = reg();
        r.apply(remote_ev("h", None, "a", &[("TMUX_PANE", "%remote")]));
        assert!(matches!(r.route(1), Route::Unsupported));
        assert!(matches!(r.route(99), Route::Unknown));

        let id = r.new_attach_id();
        let win = HostCtx {
            env: [("KITTY_WINDOW_ID".to_string(), "7".to_string())].into(),
            ..Default::default()
        };
        r.add_attach(id, "/x/remote-1.sock".into(), win);
        r.apply(remote_ev("h", Some(id), "a", &[("TMUX_PANE", "%remote")]));
        let Route::Ready(_, ctx) = r.route(1) else {
            panic!("routable")
        };
        assert_eq!(
            ctx.env.get("KITTY_WINDOW_ID").map(String::as_str),
            Some("7")
        );
        assert!(!ctx.env.contains_key("TMUX_PANE"));
        assert_eq!(r.attaches()[0]["window"]["KITTY_WINDOW_ID"], "7");

        let (_, mut rx) = r.subscribe();
        r.drop_attach(id);
        assert!(matches!(rx.try_recv().unwrap(), Update::Gone(1)));
        assert!(
            matches!(r.route(1), Route::Unknown),
            "its sessions end with it"
        );
        assert_eq!(r.sid_of(&remote("h", None).key("a")), None);
        r.apply(ev(SessState::Work));
        assert!(
            matches!(r.route(2), Route::Ready(..)),
            "local stays routable"
        );
    }

    #[test]
    fn dropping_an_attach_keeps_sessions_of_other_attaches_and_remote_sock() {
        let r = reg();
        let (a, b) = (r.new_attach_id(), r.new_attach_id());
        r.add_attach(a, "/x/a.sock".into(), HostCtx::default());
        r.add_attach(b, "/x/b.sock".into(), HostCtx::default());
        r.apply(remote_ev("h", Some(a), "1", &[]));
        r.apply(remote_ev("h", Some(b), "2", &[]));
        r.apply(remote_ev("h", None, "3", &[]));
        r.drop_attach(a);
        assert!(matches!(r.route(1), Route::Unknown));
        assert!(matches!(r.route(2), Route::Ready(..)));
        assert!(matches!(r.route(3), Route::Unsupported));
        // a session that is still alive returns with its next hook
        r.apply(remote_ev("h", Some(b), "1", &[]));
        assert_eq!(r.remotes()[0]["sessions"], 3);
    }
}
