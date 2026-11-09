//! Per-link coalescing of `session_upd` deltas. Every delta costs the device a decode, a model
//! update and a UI refresh, and the link some airtime, so a session's deltas wait up to `WINDOW`
//! and are merged (last value wins per key). What the device derives from the order of deltas is
//! kept: every state change and every tool step still arrives as its own delta (see `splits`).
//! Other messages (attention, clear, gone, result, ...) are never held: the link flushes the
//! queue before sending one, so their order relative to the deltas is unchanged.

use agentlink_proto::Session;
use std::time::Duration;
use tokio::time::Instant;

pub const WINDOW: Duration = Duration::from_millis(100);

#[derive(Default)]
pub struct DeltaQueue {
    /// In arrival order of each session's oldest held delta: (merged delta, due).
    held: Vec<(Session, Instant)>,
}

/// Whether `d` must not be merged into the held delta `p`, because the device would read the
/// merged one differently from the two in sequence:
/// - two state changes: each transition can alert (done, error, input);
/// - a new step `n` after anything about a step: the device's timeline gets one entry per `n`,
///   with that step's text and result;
/// - a step's text after its result: text only updates a step that is still running.
fn splits(p: &Session, d: &Session) -> bool {
    let step = |s: &Session| s.n.is_some() || s.ok.is_some() || s.sum.is_some() || s.tool.is_some();
    (p.st.is_some() && d.st.is_some())
        || (d.n.is_some() && step(p))
        || (p.ok.is_some() && (d.sum.is_some() || d.tool.is_some()))
}

impl DeltaQueue {
    /// Holds `d` (merged into its session's held delta when possible). Returns the delta that
    /// must go out right away because `d` cannot be merged into it.
    pub fn push(&mut self, d: Session, now: Instant) -> Option<Session> {
        let Some(i) = self.held.iter().position(|(s, _)| s.sid == d.sid) else {
            self.held.push((d, now + WINDOW));
            return None;
        };
        if splits(&self.held[i].0, &d) {
            let (out, _) = self.held.remove(i);
            self.held.push((d, now + WINDOW));
            return Some(out);
        }
        self.held[i].0.merge(&d);
        None
    }

    pub fn next_due(&self) -> Option<Instant> {
        self.held.iter().map(|(_, due)| *due).min()
    }

    /// The deltas whose window has passed, oldest first.
    pub fn take_due(&mut self, now: Instant) -> Vec<Session> {
        let (due, keep) = std::mem::take(&mut self.held)
            .into_iter()
            .partition(|(_, t)| *t <= now);
        self.held = keep;
        due.into_iter().map(|(s, _)| s).collect()
    }

    /// Everything held, oldest first.
    pub fn drain(&mut self) -> Vec<Session> {
        std::mem::take(&mut self.held)
            .into_iter()
            .map(|(s, _)| s)
            .collect()
    }

    pub fn is_empty(&self) -> bool {
        self.held.is_empty()
    }

    /// Drops everything held (a snapshot is about to replace the device's state).
    pub fn clear(&mut self) {
        self.held.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use agentlink_proto::SessState;

    fn pre(sid: u64, n: u64, sum: &str) -> Session {
        let mut s = Session::delta(sid);
        (s.n, s.sum) = (Some(n), Some(sum.into()));
        s
    }

    fn post(sid: u64) -> Session {
        let mut s = Session::delta(sid);
        s.ok = Some(true);
        s
    }

    fn state(sid: u64, st: SessState) -> Session {
        let mut s = Session::delta(sid);
        (s.st, s.tst) = (Some(st), Some(1));
        s
    }

    #[test]
    fn a_step_and_its_result_merge_into_one_delta() {
        let t = Instant::now();
        let mut q = DeltaQueue::default();
        assert!(q.push(pre(1, 5, "bash · ls"), t).is_none());
        assert!(q.push(post(1), t).is_none());
        assert_eq!(q.next_due(), Some(t + WINDOW));
        assert!(q.take_due(t + WINDOW - Duration::from_millis(1)).is_empty());
        let out = q.take_due(t + WINDOW);
        assert_eq!(out.len(), 1);
        assert_eq!(
            (out[0].n, out[0].sum.as_deref(), out[0].ok),
            (Some(5), Some("bash · ls"), Some(true))
        );
        assert!(q.is_empty());
    }

    #[test]
    fn every_step_and_state_change_still_reaches_the_device_in_order() {
        let t = Instant::now();
        let mut q = DeltaQueue::default();
        q.push(pre(1, 5, "a"), t);
        q.push(post(1), t);
        // a new step after a result: the held one goes out first, unchanged
        let out = q.push(pre(1, 6, "b"), t).unwrap();
        assert_eq!((out.n, out.ok), (Some(5), Some(true)));
        // a parallel tool call: two steps back to back
        let out = q.push(pre(1, 7, "c"), t).unwrap();
        assert_eq!((out.n, out.ok), (Some(6), None));
        // text after a result never lands on the finished step
        q.push(post(1), t);
        let mut text = Session::delta(1);
        text.tool = Some("view".into());
        assert_eq!(q.push(text, t).unwrap().n, Some(7));

        let mut q = DeltaQueue::default();
        q.push(state(2, SessState::Done), t);
        let out = q.push(state(2, SessState::Work), t).unwrap();
        assert_eq!(out.st, Some(SessState::Done));
        assert_eq!(q.drain()[0].st, Some(SessState::Work));
    }

    #[test]
    fn sessions_are_held_separately_and_flushed_oldest_first() {
        let t = Instant::now();
        let mut q = DeltaQueue::default();
        q.push(pre(2, 1, "x"), t);
        q.push(pre(1, 1, "y"), t + Duration::from_millis(10));
        q.push(post(2), t + Duration::from_millis(20));
        let out = q.take_due(t + WINDOW);
        assert_eq!(out.iter().map(|s| s.sid).collect::<Vec<_>>(), [2]);
        assert_eq!(out[0].ok, Some(true));
        assert_eq!(q.drain().iter().map(|s| s.sid).collect::<Vec<_>>(), [1]);
    }
}
