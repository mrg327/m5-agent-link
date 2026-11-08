//! Driving the agent's terminal scrollback from the device (UI-V2 §6.2).
//!
//! Scroll never raises a window (the device sent `focus` on entry) and never injects keys: the
//! drivers talk to kitty's remote control and to tmux, and every other terminal is `Unsupported`.
//! The tmux layer runs before the outer-terminal layer, so a tmux pane inside kitty scrolls tmux.
//! The preview line is terminal content: it is fetched only when the device's privacy setting
//! allows it, tidied and clipped here before it leaves the daemon, and never traced or audited.

use super::{kitty, select_outer, tmux, FocusError, FocusTarget, Outer};
use crate::registry::HostCtx;
use agentlink_proto::{ScrollPos, ScrollUnit};
use async_trait::async_trait;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

/// Clamp per command (UI-V2 §6.2): a runaway burst cannot ask for a million lines.
pub const MAX_LINES: i64 = 500;
pub const MAX_PAGES: i64 = 20;
/// Longest preview the daemon ever sends.
pub const PREVIEW_CHARS: usize = 48;

#[async_trait]
pub trait ScrollDriver: Send + Sync {
    /// Moves `target`'s terminal by `delta` units and reports where it ended up. `preview` asks
    /// for the top visible line; the caller has already checked the device's privacy setting.
    async fn scroll(
        &self,
        target: &FocusTarget,
        unit: ScrollUnit,
        delta: i64,
        preview: bool,
    ) -> Result<ScrollPos, FocusError>;
}

/// `delta` clamped to the per-command limit for its unit; `ToEnd` keeps only its sign.
pub fn clamp_delta(unit: ScrollUnit, delta: i64) -> i64 {
    match unit {
        ScrollUnit::Lines => delta.clamp(-MAX_LINES, MAX_LINES),
        ScrollUnit::Pages => delta.clamp(-MAX_PAGES, MAX_PAGES),
        ScrollUnit::ToEnd => delta.signum(),
    }
}

/// One line of terminal text made safe for the watch: control and format characters dropped,
/// runs of whitespace collapsed, trimmed, and cut at `PREVIEW_CHARS` characters (with an
/// ellipsis when something was cut). Empty when nothing printable is left.
pub fn tidy_preview(raw: &str) -> String {
    let mut out = String::new();
    let mut space = true; // leading whitespace is dropped
    for c in raw.chars() {
        // tabs and newlines are controls too: they count as whitespace, not as noise to drop
        if c.is_whitespace() {
            if !space {
                out.push(' ');
                space = true;
            }
            continue;
        }
        if c.is_control()
            || matches!(c, '\u{200b}'..='\u{200f}' | '\u{2028}'..='\u{202e}' | '\u{feff}')
        {
            continue;
        }
        space = false;
        out.push(c);
    }
    while out.ends_with(' ') {
        out.pop();
    }
    if out.chars().count() > PREVIEW_CHARS {
        out = out.chars().take(PREVIEW_CHARS - 1).collect();
        out.push('…');
    }
    out
}

/// Position bookkeeping for terminals whose API reports no scroll offset (kitty): the daemon
/// sums what it asked for, per window, and resets on `end`. Exact totals come from the text
/// dump when the preview is fetched; otherwise the estimate is all the device gets.
#[derive(Default)]
pub struct Tracked {
    lines_above: Mutex<HashMap<String, u64>>,
}

impl Tracked {
    /// Applies a line move to `key` and returns the new estimate (never below zero; `total`,
    /// when known, is the ceiling).
    pub fn apply(&self, key: &str, delta_lines: i64, total: Option<u64>) -> u64 {
        let mut m = self.lines_above.lock().unwrap();
        let cur = m.entry(key.to_owned()).or_insert(0);
        let mut next = (*cur as i64 - delta_lines).max(0) as u64;
        if let Some(t) = total {
            next = next.min(t);
        }
        *cur = next;
        next
    }

    pub fn set(&self, key: &str, lines_above: u64) {
        self.lines_above
            .lock()
            .unwrap()
            .insert(key.to_owned(), lines_above);
    }

    pub fn get(&self, key: &str) -> u64 {
        self.lines_above
            .lock()
            .unwrap()
            .get(key)
            .copied()
            .unwrap_or(0)
    }
}

pub struct SystemScroll {
    tracked: Arc<Tracked>,
}

#[async_trait]
impl ScrollDriver for SystemScroll {
    async fn scroll(
        &self,
        t: &FocusTarget,
        unit: ScrollUnit,
        delta: i64,
        preview: bool,
    ) -> Result<ScrollPos, FocusError> {
        let ctx = t.ctx.clone();
        let tracked = self.tracked.clone();
        tokio::task::spawn_blocking(move || {
            scroll_blocking(&ctx, std::env::consts::OS, &tracked, unit, delta, preview)
        })
        .await
        .map_err(|e| FocusError::Internal(e.to_string()))?
    }
}

pub fn scroll_blocking(
    ctx: &HostCtx,
    os: &str,
    tracked: &Tracked,
    unit: ScrollUnit,
    delta: i64,
    preview: bool,
) -> Result<ScrollPos, FocusError> {
    let delta = clamp_delta(unit, delta);
    if let Some(pane) = tmux::pane(ctx) {
        return tmux::scroll(ctx, pane, unit, delta, preview);
    }
    match select_outer(ctx, os) {
        Outer::Kitty => kitty::scroll(ctx, tracked, unit, delta, preview),
        // Terminal.app and Windows Terminal have no scroll API; key injection is out of scope.
        _ => Err(FocusError::Unsupported),
    }
}

pub fn default_driver() -> Arc<dyn ScrollDriver> {
    Arc::new(SystemScroll {
        tracked: Arc::new(Tracked::default()),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn deltas_are_clamped_per_unit() {
        assert_eq!(clamp_delta(ScrollUnit::Lines, -3), -3);
        assert_eq!(clamp_delta(ScrollUnit::Lines, 100_000), MAX_LINES);
        assert_eq!(clamp_delta(ScrollUnit::Lines, -100_000), -MAX_LINES);
        assert_eq!(clamp_delta(ScrollUnit::Pages, 50), MAX_PAGES);
        assert_eq!(clamp_delta(ScrollUnit::ToEnd, 99), 1);
        assert_eq!(clamp_delta(ScrollUnit::ToEnd, -7), -1);
        assert_eq!(clamp_delta(ScrollUnit::ToEnd, 0), 0);
    }

    #[test]
    fn preview_is_tidied_and_clipped() {
        assert_eq!(
            tidy_preview("  $ npm   test\t-- --watch=false \n"),
            "$ npm test -- --watch=false"
        );
        assert_eq!(
            tidy_preview("\x1b[31mred\x1b[0m\u{200b} text\r"),
            "[31mred[0m text"
        );
        assert_eq!(tidy_preview("\n\t \u{7}"), "");
        let long = "x".repeat(100);
        let t = tidy_preview(&long);
        assert_eq!(t.chars().count(), PREVIEW_CHARS);
        assert!(t.ends_with('…'));
        let exact = "y".repeat(PREVIEW_CHARS);
        assert_eq!(tidy_preview(&exact), exact);
        // multi-byte characters count as one each
        let cjk = "漢".repeat(60);
        assert_eq!(tidy_preview(&cjk).chars().count(), PREVIEW_CHARS);
    }

    #[test]
    fn tracked_position_sums_and_clamps() {
        let t = Tracked::default();
        assert_eq!(t.apply("id:1", -30, None), 30); // up 30 lines = 30 above the bottom
        assert_eq!(t.apply("id:1", -30, Some(45)), 45); // total known: cannot exceed it
        assert_eq!(t.apply("id:1", 100, None), 0); // down past the bottom clamps at live
        assert_eq!(t.get("id:1"), 0);
        assert_eq!(t.get("id:2"), 0);
        t.set("id:2", 7);
        assert_eq!(t.get("id:2"), 7);
    }

    #[test]
    fn unknown_terminals_are_unsupported() {
        let ctx = HostCtx::default();
        let t = Tracked::default();
        assert!(matches!(
            scroll_blocking(&ctx, "macos", &t, ScrollUnit::Lines, -3, false),
            Err(FocusError::Unsupported)
        ));
        let mut wt = HostCtx::default();
        wt.env.insert("WT_SESSION".into(), "x".into());
        assert!(matches!(
            scroll_blocking(&wt, "windows", &t, ScrollUnit::Pages, 1, false),
            Err(FocusError::Unsupported)
        ));
    }
}
