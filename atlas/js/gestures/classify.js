// JS mirror of firmware/components/core/src/gesture.c (the recogniser) and of the tap-deferral
// in firmware/components/core/src/app.c (dispatch / drain_input). Keep it honest: when those C
// files change, change this too. Thresholds are passed in from facts.json, never hard-coded.
//
// Time is explicit (milliseconds), as in the C code, so it is deterministic and testable.

const SWIPE = { right: 'swipe_r', left: 'swipe_l', down: 'swipe_d', up: 'swipe_u' };

/**
 * @param {(evt:{kind:string,x:number,y:number,dur_ms:number})=>void} cb
 * @param {object} th facts.gesture
 * Kinds: press, release, tap, double_tap, long_press, swipe_u/d/l/r, btn_a, btn_b, btn_a_long, btn_b_long
 */
export function createClassifier(cb, th) {
  const g = {
    pressed: false, moved: false, longFired: false, tDown: 0,
    x0: 0, y0: 0, x: 0, y: 0,
    haveTap: false, tTapUp: 0, xTap: 0, yTap: 0,
    btn: [{ down: false, longFired: false, tDown: 0 }, { down: false, longFired: false, tDown: 0 }],
  };
  const emit = (kind, x, y, dur) => cb({ kind, x: Math.trunc(x), y: Math.trunc(y), dur_ms: dur });

  function finishTouch(t) {
    const dur = t - g.tDown;
    g.pressed = false;
    emit('release', g.x, g.y, dur);
    if (g.longFired) return;

    if (g.moved) {
      const dx = g.x - g.x0, dy = g.y - g.y0;
      if (dur > th.swipe_max_ms || (Math.abs(dx) < th.swipe_min_px && Math.abs(dy) < th.swipe_min_px)) return;
      const k = Math.abs(dx) > Math.abs(dy) ? (dx > 0 ? SWIPE.right : SWIPE.left) : (dy > 0 ? SWIPE.down : SWIPE.up);
      emit(k, g.x0, g.y0, dur);
      g.haveTap = false;
      return;
    }

    const r = th.second_tap_radius_px;
    const second = g.haveTap && (g.tDown - g.tTapUp) <= th.double_tap_ms &&
      Math.abs(g.x0 - g.xTap) <= r && Math.abs(g.y0 - g.yTap) <= r;
    if (second) {
      g.haveTap = false;
      emit('double_tap', g.x0, g.y0, dur);
    } else {
      g.haveTap = true;
      g.tTapUp = t;
      g.xTap = g.x0;
      g.yTap = g.y0;
      emit('tap', g.x0, g.y0, dur);
    }
  }

  return {
    touch(down, x, y, t) {
      if (down && !g.pressed) {
        g.pressed = true;
        g.moved = g.longFired = false;
        g.tDown = t;
        g.x0 = g.x = Math.trunc(x);
        g.y0 = g.y = Math.trunc(y);
        emit('press', x, y, 0);
      } else if (down) {
        g.x = Math.trunc(x);
        g.y = Math.trunc(y);
        if (Math.abs(x - g.x0) > th.tap_slop_px || Math.abs(y - g.y0) > th.tap_slop_px) g.moved = true;
      } else if (g.pressed) {
        finishTouch(t);
      }
    },
    button(btn, down, t) {
      if (btn < 0 || btn > 1) return;
      const b = g.btn[btn];
      if (down) {
        b.down = true; b.longFired = false; b.tDown = t;
      } else if (b.down) {
        b.down = false;
        if (!b.longFired) emit(btn ? 'btn_b' : 'btn_a', 0, 0, t - b.tDown);
      }
    },
    tick(t) {
      if (g.pressed && !g.moved && !g.longFired && t - g.tDown >= th.long_press_ms) {
        g.longFired = true;
        g.haveTap = false;
        emit('long_press', g.x0, g.y0, th.long_press_ms);
      }
      for (let i = 0; i < 2; i++) {
        const b = g.btn[i];
        if (b.down && !b.longFired && t - b.tDown >= th.long_press_ms) {
          b.longFired = true;
          emit(i ? 'btn_b_long' : 'btn_a_long', 0, 0, th.long_press_ms);
        }
      }
    },
    get state() { return { ...g, btn: g.btn.map((b) => ({ ...b })) }; },
  };
}

/**
 * Mirrors app.c dispatch(): a tap the screen says "navigates" is held back, and fired by drain
 * once the double-tap window plus slack has passed with the finger up. Any other non-raw gesture
 * flushes it first; a double-tap discards it.
 * @param {(evt)=>void} dispatchNow receives events that reach the UI
 * @param {object} th facts.gesture
 * @param {(evt)=>boolean} tapDefers ui_tap_defers(): does this tap navigate on the current screen?
 */
export function createDeferrer(dispatchNow, th, tapDefers) {
  let held = null, heldAt = 0;
  const flush = () => {
    if (!held) return;
    const e = held; held = null;
    dispatchNow({ ...e, deferred: true });
  };
  return {
    dispatch(evt, now) {
      const raw = evt.kind === 'press' || evt.kind === 'release';
      if (evt.kind === 'double_tap') held = null;
      else if (!raw) flush();
      if (evt.kind === 'tap' && tapDefers(evt)) {
        held = evt; heldAt = now;
        return;
      }
      dispatchNow(evt);
    },
    tick(now, pressed) {
      if (held && !pressed && now - heldAt >= th.tap_defer_ms) flush();
    },
    get pending() { return held; },
    get heldAt() { return heldAt; },
  };
}
