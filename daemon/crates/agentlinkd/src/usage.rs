//! Usage and context gauges (docs/design/GAUGES.md), fed by Claude Code's status line.
//!
//! `agentlink-hook statusline` forwards the status-line JSON (trimmed to the fields below) as a hook line
//! with `event: "statusline"`. Field names, from https://code.claude.com/docs/en/statusline:
//!
//! - `rate_limits.five_hour` / `rate_limits.seven_day`: `{used_percentage: 0-100, resets_at: unix s}`.
//!   Only for subscribers, only after the session's first API response, each window may be absent, and
//!   Claude Code drops a window once its `resets_at` passes.
//! - `context_window.used_percentage`: may be `null` early in a session and after `/compact`.
//! - `model.display_name` (`"Opus"`), `model.id`.
//! - `pr.number`, `pr.review_state` (`approved` / `pending` / `changes_requested` / `draft`), `pr.kind`.
//!
//! The permission mode is not in the status line; it comes with the hooks (`permission_mode`,
//! hook_source.rs).
//!
//! Rate limits belong to the account, not to one session: every session reports the same figures, and a
//! session that has not had a response yet reports none. So a missing window never clears a known one;
//! the figures age instead (the device shows a dash once a reading is older than `STALE_S` or past its
//! reset). Only this machine's sessions feed the gauges: a remote host may run under another account.

use agentlink_proto::{PrState, Usage, CTX_UNKNOWN};
use serde_json::Value;
use std::collections::VecDeque;

/// The device treats a reading older than this as unknown (GAUGES.md §2). Readings are re-published at
/// least this often while they keep arriving, so a busy user never sees the dash.
pub const STALE_S: u64 = 3600;
/// Publish a reading whose figures did not change at most this often (to refresh `at`).
const REFRESH_S: u64 = 300;
/// A projection that moved by less than this is not news.
const PACE_SLACK_S: u64 = 300;
/// History kept per window, one point a minute at most.
const MIN_SPACING_S: u64 = 60;
const MAX_POINTS: usize = 240;

/// What one status-line run said about its session (`None` = the line did not say).
#[derive(Debug, Default, Clone, PartialEq)]
pub struct Readout {
    /// Percent 0-100, or `CTX_UNKNOWN` when the line has no figure (early in a session, after `/compact`).
    pub ctx: Option<u64>,
    /// Short model name, <= 16 chars.
    pub mdl: Option<String>,
    /// `(number, state)`; `(0, None)` when the line has no pull request (none, merged or closed).
    pub pr: Option<(u64, PrState)>,
}

/// One rate-limit window as read: used percent (may carry decimals) and its reset time.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Window {
    pub pct: f64,
    pub resets_at: u64,
}

/// Both windows of one status-line run.
#[derive(Debug, Default, Clone, Copy, PartialEq)]
pub struct Rates {
    pub five_hour: Option<Window>,
    pub seven_day: Option<Window>,
}

fn window(v: &Value) -> Option<Window> {
    let pct = v["used_percentage"].as_f64()?;
    let resets_at = v["resets_at"].as_u64().or_else(|| {
        // tolerate a float epoch
        v["resets_at"]
            .as_f64()
            .filter(|t| *t > 0.0)
            .map(|t| t as u64)
    })?;
    (pct.is_finite() && pct >= 0.0 && resets_at > 0).then_some(Window {
        pct: pct.min(100.0),
        resets_at,
    })
}

/// The rate-limit windows of a status-line payload; absent windows are `None`.
pub fn rates_of(payload: &Value) -> Rates {
    let r = &payload["rate_limits"];
    Rates {
        five_hour: window(&r["five_hour"]),
        seven_day: window(&r["seven_day"]),
    }
}

/// "Opus", "Sonnet 4.5", "Opus 4.7 (1M context)" -> "Opus", "Sonnet 4.5", "Opus 4.7": the display name
/// without a leading "Claude " and without a parenthesised note, at most 16 chars. Falls back to the id
/// (`claude-opus-4-7` -> "Opus 4.7").
pub fn short_model(model: &Value) -> Option<String> {
    let name = model["display_name"]
        .as_str()
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(|s| {
            let s = s.strip_prefix("Claude ").unwrap_or(s);
            s.split(" (").next().unwrap_or(s).trim().to_owned()
        })
        .or_else(|| model_from_id(model["id"].as_str()?))?;
    Some(crate::registry::clip(&name, 16)).filter(|s| !s.is_empty())
}

/// `claude-opus-4-7[-date]` -> "Opus 4.7"; `None` for an id of another shape.
fn model_from_id(id: &str) -> Option<String> {
    let mut parts = id.strip_prefix("claude-")?.split('-');
    let family = parts.next()?;
    let mut name: String = family
        .chars()
        .enumerate()
        .map(|(i, c)| if i == 0 { c.to_ascii_uppercase() } else { c })
        .collect();
    let nums: Vec<&str> = parts
        .take_while(|p| p.len() <= 2 && p.chars().all(|c| c.is_ascii_digit()))
        .collect();
    if !nums.is_empty() {
        name.push(' ');
        name.push_str(&nums.join("."));
    }
    Some(name)
}

/// The per-session readouts of a status-line payload.
pub fn readout_of(payload: &Value) -> Readout {
    let ctx = payload["context_window"]["used_percentage"]
        .as_f64()
        .filter(|p| p.is_finite() && *p >= 0.0)
        .map_or(CTX_UNKNOWN, |p| p.min(100.0) as u64);
    let pr = match payload["pr"]["number"].as_u64().filter(|n| *n > 0) {
        Some(n) => (
            n,
            PrState::from_claude(payload["pr"]["review_state"].as_str()),
        ),
        None => (0, PrState::None),
    };
    Readout {
        ctx: Some(ctx),
        mdl: short_model(&payload["model"]),
        pr: Some(pr),
    }
}

/// How a window's pace is judged.
#[derive(Debug, Clone, Copy)]
struct Spec {
    /// Window length (the window started `len` before it resets).
    len: u64,
    /// The recent burn rate is measured over a span of at least `recent_min` and at most `recent_max`.
    recent_min: u64,
    recent_max: u64,
    /// Without such a span, the average since the window began, once it is this old.
    avg_min: u64,
}

const FIVE_HOUR: Spec = Spec {
    len: 5 * 3600,
    recent_min: 10 * 60,
    recent_max: 60 * 60,
    avg_min: 15 * 60,
};
const SEVEN_DAY: Spec = Spec {
    len: 7 * 86400,
    recent_min: 3600,
    recent_max: 86400,
    avg_min: 6 * 3600,
};

/// Readings of one window, oldest first: `(at, pct)`.
#[derive(Debug, Default)]
struct History {
    resets_at: u64,
    pts: VecDeque<(u64, f64)>,
    /// The latest reading and when it was taken.
    latest: Option<(Window, u64)>,
}

impl History {
    fn record(&mut self, w: Window, at: u64, spec: Spec) {
        let new_window = self.latest.is_none()
            || w.resets_at.abs_diff(self.resets_at) > 600
            || self.latest.is_some_and(|(l, _)| w.pct + 0.5 < l.pct);
        if new_window {
            self.pts.clear();
        }
        self.resets_at = w.resets_at;
        self.latest = Some((w, at));
        if self.pts.back().is_none_or(|(t, _)| at >= t + MIN_SPACING_S) {
            self.pts.push_back((at, w.pct));
        }
        while self
            .pts
            .front()
            .is_some_and(|(t, _)| *t + spec.recent_max + MIN_SPACING_S < at)
            || self.pts.len() > MAX_POINTS
        {
            self.pts.pop_front();
        }
    }

    /// The latest reading unless its window has reset by `now`.
    fn current(&self, now: u64) -> Option<(Window, u64)> {
        self.latest.filter(|(w, _)| w.resets_at > now)
    }

    /// When the window reaches 100 % at the current pace, if that is before it resets.
    fn runs_out(&self, now: u64, spec: Spec) -> Option<u64> {
        let (w, at) = self.current(now)?;
        if w.pct >= 100.0 {
            return None; // already out: the device says so from the figure itself
        }
        let recent = self
            .pts
            .iter()
            .find(|(t, _)| *t + spec.recent_max >= at && *t + spec.recent_min <= at);
        let rate = match recent {
            Some(&(t, p)) => (w.pct - p) / (at - t) as f64,
            None => {
                let start = w.resets_at.saturating_sub(spec.len);
                let age = at.saturating_sub(start);
                if age < spec.avg_min {
                    return None;
                }
                w.pct / age as f64
            }
        };
        if rate.is_nan() || rate <= 0.0 {
            return None;
        }
        let out = at as f64 + (100.0 - w.pct) / rate;
        // to the minute, so the projection does not churn the link every few seconds
        let out = (out / 60.0).ceil() as u64 * 60;
        (out < w.resets_at).then_some(out)
    }
}

/// The account's two windows and what was last published.
#[derive(Debug, Default)]
pub struct Tracker {
    h5: History,
    wk: History,
    last: Option<Usage>,
    last_sent_at: u64,
}

impl Tracker {
    /// Records one status-line run at `now`; returns the `usage` body to publish, if any.
    pub fn record(&mut self, r: Rates, now: u64) -> Option<Usage> {
        if r.five_hour.is_none() && r.seven_day.is_none() {
            return None; // this session has no figures (yet): keep what another one said
        }
        if let Some(w) = r.five_hour {
            self.h5.record(w, now, FIVE_HOUR);
        }
        if let Some(w) = r.seven_day {
            self.wk.record(w, now, SEVEN_DAY);
        }
        let u = self.body(now);
        let same = self.last.as_ref().is_some_and(|l| same_figures(l, &u));
        if same && now < self.last_sent_at + REFRESH_S {
            return None;
        }
        self.last = Some(u.clone());
        self.last_sent_at = now;
        Some(u)
    }

    /// The usage body as of `now`: windows that have reset are left out.
    pub fn body(&self, now: u64) -> Usage {
        let pct = |w: Window| w.pct.floor().clamp(0.0, 100.0) as u64;
        let h5 = self.h5.current(now);
        let wk = self.wk.current(now);
        let at = [h5.map(|x| x.1), wk.map(|x| x.1)]
            .into_iter()
            .flatten()
            .max()
            .unwrap_or(now);
        Usage {
            at,
            h5: h5.map(|x| pct(x.0)),
            h5r: h5.map(|x| x.0.resets_at),
            h5o: self.h5.runs_out(now, FIVE_HOUR),
            wk: wk.map(|x| pct(x.0)),
            wkr: wk.map(|x| x.0.resets_at),
            wko: self.wk.runs_out(now, SEVEN_DAY),
        }
    }

    /// What a device that connects at `now` is sent: the last published body, unless it is stale.
    pub fn last(&self, now: u64) -> Option<Usage> {
        self.last.clone().filter(|u| now < u.at + STALE_S)
    }
}

/// Whether two bodies show the same thing: the reading time aside, and projections that moved by less
/// than `PACE_SLACK_S` (the pace wobbles by a minute from one reading to the next).
fn same_figures(a: &Usage, b: &Usage) -> bool {
    let near = |x: Option<u64>, y: Option<u64>| match (x, y) {
        (Some(x), Some(y)) => x.abs_diff(y) < PACE_SLACK_S,
        (x, y) => x == y,
    };
    (a.h5, a.h5r, a.wk, a.wkr) == (b.h5, b.h5r, b.wk, b.wkr)
        && near(a.h5o, b.h5o)
        && near(a.wko, b.wko)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    const T0: u64 = 1_800_000_000;

    fn rates(h5: Option<(f64, u64)>, wk: Option<(f64, u64)>) -> Rates {
        let w = |(pct, resets_at)| Window { pct, resets_at };
        Rates {
            five_hour: h5.map(w),
            seven_day: wk.map(w),
        }
    }

    #[test]
    fn parses_the_documented_status_line() {
        // the "Full JSON schema" example of the status-line docs, trimmed
        let v = json!({
            "session_id": "abc123", "model": {"id": "claude-opus-4-7", "display_name": "Opus"},
            "context_window": {"used_percentage": 8, "remaining_percentage": 92},
            "rate_limits": {"five_hour": {"used_percentage": 23.5, "resets_at": 1738425600u64},
                            "seven_day": {"used_percentage": 41.2, "resets_at": 1738857600u64}},
            "pr": {"number": 1234, "url": "https://example.invalid/pull/1234", "review_state": "pending"}
        });
        let r = rates_of(&v);
        assert_eq!(
            r.five_hour,
            Some(Window {
                pct: 23.5,
                resets_at: 1738425600
            })
        );
        assert_eq!(r.seven_day.map(|w| w.resets_at), Some(1738857600));
        assert_eq!(
            readout_of(&v),
            Readout {
                ctx: Some(8),
                mdl: Some("Opus".into()),
                pr: Some((1234, PrState::Pending)),
            }
        );
    }

    #[test]
    fn absent_and_null_values_are_unknown_never_zero() {
        // no rate_limits (not a subscriber, or before the first response); context null early on
        let v = json!({"session_id": "s", "context_window": {"used_percentage": null}});
        assert_eq!(rates_of(&v), Rates::default());
        let r = readout_of(&v);
        assert_eq!(r.ctx, Some(CTX_UNKNOWN));
        assert_eq!(r.mdl, None, "no model: leave the last one");
        assert_eq!(r.pr, Some((0, PrState::None)), "no pr object: none open");
        // one window only, and a malformed one
        let v = json!({"rate_limits": {"seven_day": {"used_percentage": 3, "resets_at": 99}, "five_hour": {"resets_at": 5}}});
        let r = rates_of(&v);
        assert!(r.five_hour.is_none());
        assert_eq!(
            r.seven_day,
            Some(Window {
                pct: 3.0,
                resets_at: 99
            })
        );
        // the tracker publishes nothing for a run without figures
        let mut t = Tracker::default();
        assert_eq!(t.record(Rates::default(), T0), None);
        assert_eq!(t.last(T0), None);
    }

    #[test]
    fn short_model_names() {
        let m = |d: &str| short_model(&json!({"display_name": d}));
        assert_eq!(m("Opus").as_deref(), Some("Opus"));
        assert_eq!(m("Claude Sonnet 4.5").as_deref(), Some("Sonnet 4.5"));
        assert_eq!(m("Opus 4.7 (1M context)").as_deref(), Some("Opus 4.7"));
        assert_eq!(
            m("A very long model display name").as_deref(),
            Some("A very long mod…")
        );
        assert_eq!(
            short_model(&json!({"id": "claude-sonnet-4-5-20250929"})).as_deref(),
            Some("Sonnet 4.5")
        );
        assert_eq!(short_model(&json!({"id": "gpt-x"})), None);
        assert_eq!(short_model(&json!({})), None);
    }

    #[test]
    fn review_states() {
        let pr = |s: Value| readout_of(&json!({"pr": s})).pr;
        assert_eq!(
            pr(json!({"number": 7, "review_state": "approved"})),
            Some((7, PrState::Approved))
        );
        assert_eq!(
            pr(json!({"number": 7, "review_state": "changes_requested"})),
            Some((7, PrState::Changes))
        );
        assert_eq!(
            pr(json!({"number": 7, "review_state": "draft"})),
            Some((7, PrState::Draft))
        );
        assert_eq!(pr(json!({"number": 7})), Some((7, PrState::None)));
        assert_eq!(
            pr(json!({"number": 7, "kind": "mr", "review_state": "weird"})),
            Some((7, PrState::None))
        );
    }

    #[test]
    fn publishes_on_change_and_refreshes_at() {
        let mut t = Tracker::default();
        let reset = T0 + 4 * 3600;
        let u = t.record(rates(Some((42.7, reset)), None), T0).unwrap();
        assert_eq!((u.at, u.h5, u.h5r, u.wk), (T0, Some(42), Some(reset), None));
        // the same figures a moment later: nothing to send
        assert_eq!(t.record(rates(Some((42.9, reset)), None), T0 + 30), None);
        // a new whole percent: sent
        let u = t.record(rates(Some((43.0, reset)), None), T0 + 40).unwrap();
        assert_eq!(u.h5, Some(43));
        // unchanged but REFRESH_S later: sent again with a fresh `at`
        let u = t
            .record(rates(Some((43.1, reset)), None), T0 + 40 + REFRESH_S)
            .unwrap();
        assert_eq!(u.at, T0 + 40 + REFRESH_S);
        // another session without figures keeps them
        assert_eq!(t.record(Rates::default(), T0 + 500), None);
        assert_eq!(t.last(T0 + 500).unwrap().h5, Some(43));
        assert_eq!(
            t.last(T0 + 340 + STALE_S),
            None,
            "a stale reading is not handed to a new device"
        );
    }

    #[test]
    fn a_reset_window_is_left_out() {
        let mut t = Tracker::default();
        t.record(rates(Some((50.0, T0 + 100)), Some((20.0, T0 + 86400))), T0)
            .unwrap();
        let b = t.body(T0 + 100);
        assert_eq!((b.h5, b.h5r, b.h5o), (None, None, None));
        assert_eq!(b.wk, Some(20));
    }

    #[test]
    fn pace_from_the_recent_burn_rate() {
        let mut t = Tracker::default();
        let reset = T0 + 3 * 3600; // 2 h into the window
                                   // 40 % -> 60 % in 20 minutes: 1 % a minute, 100 % in 40 more minutes, long before the reset
        t.record(rates(Some((40.0, reset)), None), T0);
        t.record(rates(Some((50.0, reset)), None), T0 + 600);
        let u = t
            .record(rates(Some((60.0, reset)), None), T0 + 1200)
            .unwrap();
        assert_eq!(u.h5o, Some(T0 + 1200 + 40 * 60));
        // idle since: no reading in the last hour shows a rise, and the window average runs out after the reset
        let u = t.record(rates(Some((60.0, reset)), None), T0 + 3 * 3600 - 60);
        assert_eq!(u.and_then(|u| u.h5o), None);
    }

    #[test]
    fn pace_from_the_window_average_without_history() {
        let mut t = Tracker::default();
        // one reading, 1 h into the window, 30 % used: 30 %/h, 100 % at +2h20m, before the reset at +4 h
        let reset = T0 + 4 * 3600;
        let u = t.record(rates(Some((30.0, reset)), None), T0).unwrap();
        assert_eq!(u.h5o, Some(T0 + 8400));
        // 10 % used after 1 h: 100 % in 9 h, after the reset: no projection
        let mut t = Tracker::default();
        let u = t.record(rates(Some((10.0, reset)), None), T0).unwrap();
        assert_eq!(u.h5o, None);
        // too early in the window to judge
        let mut t = Tracker::default();
        let u = t
            .record(rates(Some((10.0, T0 + 5 * 3600 - 60)), None), T0)
            .unwrap();
        assert_eq!(u.h5o, None);
        // at 100 %: no projection, the figure says it
        let mut t = Tracker::default();
        let u = t.record(rates(Some((100.0, reset)), None), T0).unwrap();
        assert_eq!((u.h5, u.h5o), (Some(100), None));
    }

    #[test]
    fn a_new_window_starts_a_new_history() {
        let mut t = Tracker::default();
        let r1 = T0 + 600;
        t.record(rates(Some((90.0, r1)), None), T0);
        // the window reset: a low figure with a later reset time; the old points must not make a negative rate
        let r2 = T0 + 5 * 3600 + 600;
        let u = t.record(rates(Some((2.0, r2)), None), T0 + 700).unwrap();
        assert_eq!((u.h5, u.h5r, u.h5o), (Some(2), Some(r2), None));
    }
}
