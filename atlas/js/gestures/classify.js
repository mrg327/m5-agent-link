// JS mirror of firmware/components/core/src/gesture.c: the touch recogniser, the rim-scrub
// recogniser, the centre drag stream with the release-speed flick (SCROLL, UI-V2 §4.4) and the
// button logic (A double-press, A+B chord). Keep it honest: when that C file changes, change this
// too. Thresholds come from facts.json, never hard-coded. There is no tap deferral any more: a tap
// fires on release.
//
// Time is explicit (milliseconds), as in the C code, so it is deterministic and testable.

const SWIPE = { right: 'swipe_r', left: 'swipe_l', down: 'swipe_d', up: 'swipe_u' };

const wrap180 = (d) => { while (d > 180) d -= 360; while (d <= -180) d += 360; return d; };
const HIST = 8; // GESTURE_HIST: recent samples kept for the release speed

/** Angle of a point around the screen centre, 0..360 from 12 o'clock, clockwise (gesture_angle_deg). */
export function angleDeg(th, x, y) {
  const dx = x - th.cx_px, dy = y - th.cy_px;
  let deg = Math.atan2(dy, dx) * 180 / Math.PI + 90;
  deg = deg < 0 ? deg + 360 : deg >= 360 ? deg - 360 : deg;
  return { deg, r: Math.hypot(dx, dy) };
}

/**
 * @param {(evt:{kind:string,x:number,y:number,dur_ms:number})=>void} cb
 * @param {object} th facts.gesture
 * Kinds: press, release, tap, long_press, swipe_u/d/l/r (with dx, dy = whole travel and flick =
 * released at flick speed), drag (x, y = press point, dy = the step, up < 0; one per drag_step_px of
 * vertical travel from a press inside the rim), scrub_start, scrub, scrub_end (x = sector, y = angle,
 * dur_ms = arc swept), btn_a, btn_b, btn_a_long, btn_b_long, btn_a_double, chord
 */
export function createClassifier(cb, th) {
  const g = {
    pressed: false, moved: false, longFired: false, tDown: 0,
    x0: 0, y0: 0, x: 0, y: 0,
    sectors: 0, rimCandidate: false, scrubbing: false, lastDeg: 0, arcDeg: 0, sector: 0,
    centreStart: false, dragging: false, dragY: 0, hist: [],
    aPending: false, aReleaseMs: 0,
    btn: [0, 1].map(() => ({ down: false, longFired: false, consumed: false, second: false, tDown: 0 })),
  };
  const emit = (kind, x, y, dur, extra) => cb({ kind, x: Math.trunc(x), y: Math.trunc(y), dur_ms: Math.trunc(dur), ...(extra || {}) });
  const sectorOf = (deg) => {
    if (g.sectors < 2) return 0;
    const s = Math.trunc(deg * g.sectors / 360);
    return s >= g.sectors ? g.sectors - 1 : s;
  };

  // Changes sector only once the finger is scrub_hyst_deg past the boundary (capped at a third of a sector).
  function updateSector(deg) {
    if (g.sectors < 2) return;
    const width = 360 / g.sectors;
    const centre = (g.sector + 0.5) * width;
    if (Math.abs(wrap180(deg - centre)) <= width / 2 + Math.min(th.scrub_hyst_deg, width / 3)) return;
    const next = sectorOf(deg);
    if (next === g.sector) return;
    g.sector = next;
    emit('scrub', next, deg, Math.abs(g.arcDeg));
  }

  function rimTrack(x, y) {
    const { deg, r } = angleDeg(th, x, y);
    if (r < th.rim_track_in_px || r > th.rim_track_out_px) {
      if (!g.scrubbing) g.rimCandidate = false;
      return;
    }
    g.arcDeg += wrap180(deg - g.lastDeg);
    g.lastDeg = deg;
    if (g.scrubbing) {
      updateSector(deg);
    } else if (Math.abs(g.arcDeg) >= th.scrub_start_deg) {
      g.scrubbing = true;
      g.sector = sectorOf(deg);
      emit('scrub_start', g.sector, deg, Math.abs(g.arcDeg));
    }
  }

  const histPush = (x, y, t) => { g.hist.push({ x: Math.trunc(x), y: Math.trunc(y), t }); if (g.hist.length > HIST) g.hist.shift(); };

  // Speed over the oldest sample still inside the flick window, in px/s (release_speed).
  function releaseSpeed(t) {
    let best = null;
    for (let k = g.hist.length - 1; k >= 0; k--) {
      if (t - g.hist[k].t > th.flick_window_ms) break;
      best = g.hist[k];
    }
    if (!best) return 0;
    const dt = t - best.t;
    if (dt === 0) return 0;
    return Math.trunc(Math.hypot(g.x - best.x, g.y - best.y) * 1000 / dt);
  }

  // Drag stream (SCROLL): every drag_step_px of vertical travel from a press inside the rim (drag_track).
  function dragTrack(y, t) {
    const d = y - g.dragY;
    if (Math.abs(d) < th.drag_step_px) return;
    const step = Math.trunc(d / th.drag_step_px) * th.drag_step_px;
    g.dragY += step;
    g.dragging = true;
    emit('drag', g.x0, g.y0, t - g.tDown, { dx: 0, dy: step });
  }

  function finishTouch(t) {
    const dur = t - g.tDown;
    g.pressed = false;
    emit('release', g.x, g.y, dur);
    if (g.scrubbing) {
      g.scrubbing = g.rimCandidate = false;
      emit('scrub_end', g.sector, g.lastDeg, Math.abs(g.arcDeg));
      return;
    }
    if (g.longFired) return;

    if (g.moved) {
      const dx = g.x - g.x0, dy = g.y - g.y0;
      if (dur > th.swipe_max_ms || (Math.abs(dx) < th.swipe_min_px && Math.abs(dy) < th.swipe_min_px)) return;
      const k = Math.abs(dx) > Math.abs(dy) ? (dx > 0 ? SWIPE.right : SWIPE.left) : (dy > 0 ? SWIPE.down : SWIPE.up);
      emit(k, g.x0, g.y0, dur, { dx, dy, flick: releaseSpeed(t) >= th.flick_px_per_s });
      return;
    }
    emit('tap', g.x0, g.y0, dur);
  }

  return {
    touch(down, x, y, t) {
      if (down && !g.pressed) {
        g.pressed = true;
        g.moved = g.longFired = g.scrubbing = false;
        g.tDown = t;
        g.x0 = g.x = Math.trunc(x);
        g.y0 = g.y = Math.trunc(y);
        const { deg, r } = angleDeg(th, x, y);
        g.lastDeg = deg;
        g.arcDeg = 0;
        g.rimCandidate = r >= th.rim_in_px && r <= th.rim_out_px;
        g.centreStart = r < th.rim_in_px;
        g.dragging = false;
        g.dragY = g.y0;
        g.hist = [];
        histPush(x, y, t);
        emit('press', x, y, 0);
      } else if (down) {
        g.x = Math.trunc(x);
        g.y = Math.trunc(y);
        histPush(x, y, t);
        if (Math.abs(x - g.x0) > th.tap_slop_px || Math.abs(y - g.y0) > th.tap_slop_px) g.moved = true;
        if (g.rimCandidate) rimTrack(x, y);
        else if (g.centreStart) dragTrack(g.y, t);
      } else if (g.pressed) {
        finishTouch(t);
      }
    },
    button(btn, down, t) {
      if (btn < 0 || btn > 1) return;
      const b = g.btn[btn], o = g.btn[1 - btn];
      if (down) {
        if (b.down) return;
        b.down = true;
        b.longFired = b.consumed = b.second = false;
        b.tDown = t;
        if (o.down && !o.longFired && !o.consumed) {
          b.consumed = o.consumed = true;
          g.aPending = false;
          emit('chord', 0, 0, 0);
          return;
        }
        if (btn === 0 && g.aPending) {
          g.aPending = false;
          b.second = t - g.aReleaseMs <= th.btn_double_ms;
        }
      } else if (b.down) {
        b.down = false;
        if (b.consumed || b.longFired) return;
        const dur = t - b.tDown;
        if (btn === 1) emit('btn_b', 0, 0, dur);
        else if (b.second) emit('btn_a_double', 0, 0, dur);
        else { g.aPending = true; g.aReleaseMs = t; }
      }
    },
    tick(t) {
      if (g.pressed && !g.moved && !g.longFired && t - g.tDown >= th.long_press_ms) {
        g.longFired = true;
        emit('long_press', g.x0, g.y0, th.long_press_ms);
      }
      if (g.aPending && t - g.aReleaseMs >= th.btn_double_ms) {
        g.aPending = false;
        emit('btn_a', 0, 0, 0);
      }
      for (let i = 0; i < 2; i++) {
        const b = g.btn[i];
        if (b.down && !b.longFired && !b.consumed && t - b.tDown >= th.long_press_ms) {
          b.longFired = true;
          emit(i ? 'btn_b_long' : 'btn_a_long', 0, 0, th.long_press_ms);
        }
      }
    },
    /** Number of equal rim sectors (gesture_set_sectors); safe mid-scrub. */
    setSectors(n) {
      g.sectors = n;
      if (g.scrubbing) g.sector = sectorOf(g.lastDeg);
    },
    get scrubbing() { return g.scrubbing; },
    get dragging() { return g.dragging; },
    get state() { return { ...g, hist: g.hist.map((s) => ({ ...s })), btn: g.btn.map((b) => ({ ...b })) }; },
  };
}
