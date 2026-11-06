//! Session registry: merges events from all `AgentSource`s into the device-visible
//! session list and broadcasts deltas.

use crate::trace::{Dir, Trace};
use agentlink_proto::{Agent, InputSub, SessState, Session};
use serde::Serialize;
use std::collections::{BTreeMap, HashMap};
use std::sync::Mutex;
use tokio::sync::broadcast;

/// Snapshot limit from the CDDL (`[0*32 session]`).
const MAX_SESSIONS: usize = 32;

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
        }
    }
}

#[derive(Debug)]
pub enum SessionEvent {
    /// `key` identifies the session within its source (e.g. `copilot:<sessionId>`).
    Update {
        key: String,
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
}

struct Record {
    session: Session,
    ctx: HostCtx,
}

#[derive(Default)]
struct State {
    next_sid: u64,
    by_key: HashMap<String, u64>,
    sessions: BTreeMap<u64, Record>,
}

pub struct Registry {
    state: Mutex<State>,
    tx: broadcast::Sender<Update>,
    trace: Trace,
}

pub fn now_unix() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn clip(s: &str, max: usize) -> String {
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
                ..Default::default()
            }),
            tx,
            trace,
        }
    }

    pub fn apply(&self, ev: SessionEvent) {
        let mut st = self.state.lock().unwrap();
        let update = match ev {
            SessionEvent::Update { key, patch, ctx } => Self::upsert(&mut st, key, patch, ctx),
            SessionEvent::End { key } => st
                .by_key
                .remove(&key)
                .and_then(|sid| st.sessions.remove(&sid).map(|_| Update::Gone(sid))),
        };
        if let Some(u) = update {
            let (kind, sid, msg) = match &u {
                Update::Upsert(s) => (
                    "session_upd",
                    s.sid,
                    serde_json::to_value(s).unwrap_or_default(),
                ),
                Update::Gone(sid) => ("gone", *sid, serde_json::json!({"sid": sid})),
            };
            self.trace
                .record("registry", Dir::Internal, kind, Some(sid), None, msg, None);
            let _ = self.tx.send(u); // no receivers is fine
        }
    }

    fn upsert(st: &mut State, key: String, p: SessionPatch, ctx: HostCtx) -> Option<Update> {
        let now = now_unix();
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
            s.host = Some(0);
            st.sessions.insert(
                sid,
                Record {
                    session: s,
                    ctx: HostCtx::default(),
                },
            );
        }
        let rec = st.sessions.get_mut(&sid)?;
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
        if !changed {
            return None;
        }
        Some(Update::Upsert(if created { s.clone() } else { d }))
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

    pub fn get(&self, sid: u64) -> Option<(Session, HostCtx)> {
        let st = self.state.lock().unwrap();
        st.sessions
            .get(&sid)
            .map(|r| (r.session.clone(), r.ctx.clone()))
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
}
