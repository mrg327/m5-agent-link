//! Coalescing of the device's scroll bursts (UI-V2 §6.2). The watch sends at most one
//! `cmd_op_scroll` per 50 ms; a terminal round trip can take longer than that, so while one
//! batch runs, everything that arrives is summed into the next. Pure, so it is unit tested
//! without a terminal; `device.rs` owns the in-flight task and the result throttle.

use crate::focus::scroll::clamp_delta;
use agentlink_proto::ScrollUnit;
use std::time::Duration;

/// The daemon answers at most one scroll `result` per device in this window; the latest wins.
pub const RESULT_MIN_GAP: Duration = Duration::from_millis(250);
/// Pending batches beyond this are dropped oldest first (a burst that outruns the terminal).
const MAX_PENDING: usize = 4;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Batch {
    pub sid: u64,
    pub unit: ScrollUnit,
    pub delta: i64,
    /// Envelope id of the newest command folded in: the `result` answers that one.
    pub re: u64,
    /// Commands folded into this batch (for the trace line).
    pub n: u32,
}

#[derive(Default)]
pub struct Coalescer {
    pending: Vec<Batch>,
    busy: bool,
}

impl Coalescer {
    /// Offers a command. Returns it when nothing is running (the caller starts it now);
    /// otherwise queues it: same session and unit as the newest pending batch → the deltas are
    /// summed; `ToEnd` replaces everything pending (an absolute move makes earlier ones moot).
    pub fn push(&mut self, b: Batch) -> Option<Batch> {
        if !self.busy {
            self.busy = true;
            return Some(b);
        }
        if b.unit == ScrollUnit::ToEnd {
            self.pending.clear();
            self.pending.push(b);
            return None;
        }
        if let Some(last) = self.pending.last_mut() {
            if last.sid == b.sid && last.unit == b.unit {
                last.delta = clamp_delta(b.unit, last.delta.saturating_add(b.delta));
                last.re = b.re;
                last.n += b.n;
                return None;
            }
        }
        if self.pending.len() == MAX_PENDING {
            self.pending.remove(0);
        }
        self.pending.push(b);
        None
    }

    /// The running batch finished. Returns the next one to start, or `None` when idle.
    pub fn done(&mut self) -> Option<Batch> {
        if self.pending.is_empty() {
            self.busy = false;
            None
        } else {
            Some(self.pending.remove(0))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn b(unit: ScrollUnit, delta: i64, re: u64) -> Batch {
        Batch {
            sid: 2,
            unit,
            delta,
            re,
            n: 1,
        }
    }

    #[test]
    fn first_runs_at_once_and_a_burst_is_summed_into_one_batch() {
        let mut c = Coalescer::default();
        assert_eq!(
            c.push(b(ScrollUnit::Lines, -3, 10)),
            Some(b(ScrollUnit::Lines, -3, 10))
        );
        for id in 11..=14 {
            assert_eq!(c.push(b(ScrollUnit::Lines, -3, id)), None);
        }
        let next = c.done().unwrap();
        assert_eq!((next.delta, next.re, next.n), (-12, 14, 4));
        assert_eq!(c.done(), None);
    }

    #[test]
    fn extreme_deltas_do_not_overflow() {
        let mut c = Coalescer::default();
        c.push(b(ScrollUnit::Lines, 1, 1));
        c.push(b(ScrollUnit::Lines, i64::MAX, 2));
        c.push(b(ScrollUnit::Lines, i64::MAX, 3));
        assert_eq!(c.done().unwrap().delta, crate::focus::scroll::MAX_LINES);
        c.push(b(ScrollUnit::Lines, i64::MIN, 4));
        c.push(b(ScrollUnit::Lines, i64::MIN, 5));
        assert_eq!(c.done().unwrap().delta, -crate::focus::scroll::MAX_LINES);
    }

    #[test]
    fn different_units_queue_separately_and_to_end_replaces() {
        let mut c = Coalescer::default();
        c.push(b(ScrollUnit::Lines, -3, 1));
        c.push(b(ScrollUnit::Lines, -3, 2));
        c.push(b(ScrollUnit::Pages, -1, 3));
        c.push(b(ScrollUnit::Pages, -1, 4));
        c.push(b(ScrollUnit::Lines, 6, 5));
        assert_eq!(
            c.done().unwrap(),
            Batch {
                sid: 2,
                unit: ScrollUnit::Lines,
                delta: -3,
                re: 2,
                n: 1
            }
        );
        assert_eq!(
            c.done().unwrap(),
            Batch {
                sid: 2,
                unit: ScrollUnit::Pages,
                delta: -2,
                re: 4,
                n: 2
            }
        );
        assert_eq!(c.done().unwrap().delta, 6);
        assert_eq!(c.done(), None);

        let mut c = Coalescer::default();
        c.push(b(ScrollUnit::Lines, -3, 1));
        c.push(b(ScrollUnit::Lines, -3, 2));
        c.push(b(ScrollUnit::Pages, -1, 3));
        c.push(b(ScrollUnit::ToEnd, 1, 4));
        assert_eq!(
            c.done().unwrap(),
            Batch {
                sid: 2,
                unit: ScrollUnit::ToEnd,
                delta: 1,
                re: 4,
                n: 1
            }
        );
        assert_eq!(c.done(), None);
    }

    #[test]
    fn summed_deltas_stay_within_the_clamp_and_other_sessions_do_not_merge() {
        let mut c = Coalescer::default();
        c.push(b(ScrollUnit::Lines, -3, 1));
        for id in 2..300 {
            c.push(b(ScrollUnit::Lines, -3, id));
        }
        assert_eq!(c.done().unwrap().delta, -500);
        let mut c = Coalescer::default();
        c.push(b(ScrollUnit::Lines, -3, 1));
        c.push(b(ScrollUnit::Lines, -3, 2));
        let mut other = b(ScrollUnit::Lines, -3, 3);
        other.sid = 9;
        c.push(other.clone());
        assert_eq!(c.done().unwrap().re, 2);
        assert_eq!(c.done().unwrap(), other);
    }

    #[test]
    fn a_runaway_burst_drops_the_oldest_pending_batches() {
        let mut c = Coalescer::default();
        c.push(b(ScrollUnit::Lines, -3, 0));
        for id in 1..=10u64 {
            let unit = if id % 2 == 0 {
                ScrollUnit::Lines
            } else {
                ScrollUnit::Pages
            };
            c.push(b(unit, -1, id));
        }
        let mut n = 0;
        while c.done().is_some() {
            n += 1;
        }
        assert_eq!(n, MAX_PENDING);
    }
}
