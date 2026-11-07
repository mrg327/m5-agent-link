// The gesture lab: a practice pad + live classifier readouts + oscilloscope timing diagram.
// Everything is driven by classify.js (a JS mirror of gesture.c and the app.c tap deferral),
// using the thresholds in facts.gesture.
import { createClassifier, createDeferrer } from './classify.js';
import { h, svg, fitCanvas, hexA, clamp, cssVar } from './dom.js';

const SCREENS = [
  { id: 'glance', label: 'Glance', defers: false, note: 'A tap acts at once: nothing else could be meant.' },
  { id: 'list', label: 'List · selected row', defers: true, note: 'A tap on the selected row opens it, so it waits in case a double-tap follows.' },
  { id: 'detail', label: 'Detail', defers: true, note: 'Every tap waits, because a double-tap here means “focus the terminal”.' },
];

const GLYPH = { swipe_u: '↑', swipe_d: '↓', swipe_l: '←', swipe_r: '→' };
const NAME = {
  tap: 'Tap', double_tap: 'Double tap', long_press: 'Long press', swipe_u: 'Swipe up', swipe_d: 'Swipe down',
  swipe_l: 'Swipe left', swipe_r: 'Swipe right',
};

export function createLab(root, ctx) {
  const th = ctx.facts.gesture;
  const W = ctx.facts.display.width;
  const colors = () => ({
    accent: cssVar('--fw-accent', ctx.facts.palette.accent),
    amber: cssVar('--fw-amber', ctx.facts.palette.amber),
    success: cssVar('--fw-success', ctx.facts.palette.success),
    muted: cssVar('--fw-muted', ctx.facts.palette.muted),
    text: cssVar('--fw-text', ctx.facts.palette.text),
    danger: cssVar('--fw-danger', ctx.facts.palette.danger),
  });

  // ── state ──
  let screen = SCREENS[2];
  let mirror = false;
  const S = {
    down: false, x0: 0, y0: 0, x: 0, y: 0, tDown: 0, tUp: 0, lastUp: null, lastHold: 0,
    trail: [], stroke: [], verdict: null, verdictAt: 0, dist: 0, moved: false, longFired: false,
    events: [], burstStart: 0, lastEvent: 0, tapAt: null, firedAt: null, gap: null, deferDt: null,
    active: false, raf: 0,
  };

  const log = (kind, label, t, extra = {}) => {
    if (!S.events.length || t - S.lastEvent > 3500) { S.events = []; S.burstStart = t; }
    S.events.push({ kind, label, t, ...extra });
    S.lastEvent = t;
  };

  const verdict = (text, tone, t, sub = '') => {
    S.verdict = { text, tone, sub };
    S.verdictAt = t;
    chip.dataset.tone = tone;
    chipText.textContent = text;
    chipSub.textContent = sub;
    chip.classList.remove('is-pop'); void chip.offsetWidth; chip.classList.add('is-pop');
  };

  // ── classifier + deferral ──
  const deferrer = createDeferrer((e) => {
    const t = performance.now();
    if (e.kind === 'tap') {
      log('fire', e.deferred ? 'tap fires' : 'tap acts', t);
      S.firedAt = t;
      if (e.deferred && S.tapAt != null) S.deferDt = Math.round(t - S.tapAt);
      verdict('Tap', 'ok', t, e.deferred ? `held back ${S.deferDt} ms, then fired` : 'acted immediately');
    } else if (e.kind === 'double_tap') {
      log('fire', 'focus on PC', t);
      verdict('Double tap', 'ok', t, 'focus the terminal on the PC');
    }
  }, th, () => screen.defers);

  const classifier = createClassifier((e) => {
    const t = performance.now();
    switch (e.kind) {
      case 'press': log('press', 'press', t); break;
      case 'release': log('release', 'release', t); break;
      case 'tap':
        S.tapAt = t; S.firedAt = null; S.deferDt = null;
        log('tap', 'tap', t);
        if (screen.defers) verdict('Tap…', 'wait', t, `held ${th.tap_defer_ms} ms in case a second tap follows`);
        break;
      case 'double_tap': S.gap = Math.round(t - (S.tapAt ?? t)); log('double', 'double tap', t); break;
      case 'long_press': log('long', 'long press', t); verdict('Long press', 'ok', t, `held ${th.long_press_ms} ms without moving`); break;
      default:
        if (e.kind.startsWith('swipe')) {
          S.fired = true;
          log('swipe', 'swipe ' + GLYPH[e.kind], t);
          verdict(NAME[e.kind], 'ok', t, `${Math.round(S.dist)} px in ${e.dur_ms} ms`);
        }
    }
    deferrer.dispatch(e, t);
  }, th);

  // ── DOM ──
  const pad = h('canvas', { class: 'lab__pad', 'aria-label': 'Gesture practice pad. Tap, double-tap, hold or swipe.', role: 'application', tabindex: 0 });
  const chipText = h('b', { class: 'chip__text', text: 'Try it' });
  const chipSub = h('span', { class: 'chip__sub', text: 'tap, double-tap, hold or swipe the pad' });
  const chip = h('div', { class: 'chip', 'data-tone': 'idle', 'aria-live': 'polite' }, chipText, chipSub);

  const num = (label, unit, limit) => {
    const v = h('b', { class: 'num__v', text: '0' });
    const bar = h('i', { class: 'num__bar' }, h('u', { class: 'num__fill' }));
    if (limit != null) bar.append(h('s', { class: 'num__mark', style: { left: limit.pos + '%' }, title: limit.label }));
    const el = h('div', { class: 'num' },
      h('div', { class: 'num__top' }, h('span', { class: 'num__l', text: label }), h('span', { class: 'num__u', text: unit })), v, bar);
    return { el, set(val, frac, hot) {
      v.textContent = String(Math.round(val));
      bar.firstChild.style.transform = `scaleX(${clamp(frac, 0, 1)})`;
      el.dataset.hot = hot ? '1' : '';
    } };
  };
  const swipeMax = th.swipe_min_px * 1.6, holdMax = th.long_press_ms * 1.2, winMax = th.tap_defer_ms * 1.25;
  const nDist = num('distance', 'px', { pos: (th.swipe_min_px / swipeMax) * 100, label: `swipe ≥ ${th.swipe_min_px} px` });
  const nHold = num('hold', 'ms', { pos: (th.long_press_ms / holdMax) * 100, label: `long press ${th.long_press_ms} ms` });
  const nWin = num('gap', 'ms', { pos: (th.double_tap_ms / winMax) * 100, label: `double-tap ${th.double_tap_ms} ms` });

  const screenBtns = SCREENS.map((s) => h('button', {
    type: 'button', class: 'seg__b', 'aria-pressed': String(s === screen), text: s.label,
    onclick: () => { screen = s; screenBtns.forEach((b, i) => b.setAttribute('aria-pressed', String(SCREENS[i] === s))); screenNote.textContent = s.note; },
  }));
  const screenNote = h('p', { class: 'lab__note', text: screen.note });
  const mirrorBtn = h('button', { type: 'button', class: 'toggle', 'aria-pressed': 'false', onclick: () => {
    mirror = !mirror; mirrorBtn.setAttribute('aria-pressed', String(mirror));
  } }, h('i', { class: 'toggle__knob' }), h('span', { text: 'Also send to the watch' }));

  const scope = h('canvas', { class: 'scope', 'aria-label': 'Timing diagram of your touches and the decisions the classifier makes', role: 'img' });

  const why = h('p', { class: 'lab__why' },
    h('b', { text: `Why ${th.tap_defer_ms} ms? ` }),
    `A tap that would navigate is held back for the ${th.double_tap_ms} ms double-tap window (plus ${th.defer_slack_ms} ms slack). ` +
    'Otherwise a double-tap meaning “focus on the PC” would first open the detail screen, then focus.');

  root.append(
    h('div', { class: 'lab' },
      h('div', { class: 'lab__main' },
        h('div', { class: 'lab__padwrap' }, pad),
        h('div', { class: 'lab__side' },
          chip,
          h('div', { class: 'nums' }, nDist.el, nHold.el, nWin.el),
          h('div', { class: 'lab__ctl' },
            h('span', { class: 'lab__ctl-l', text: 'Screen the tap lands on' }),
            h('div', { class: 'seg', role: 'group', 'aria-label': 'Screen context' }, screenBtns),
            screenNote, mirrorBtn))),
      h('div', { class: 'scopewrap' }, scope, h('div', { class: 'scope__legend' },
        h('span', { 'data-k': 'sig', text: 'finger' }),
        h('span', { 'data-k': 'win', text: `double-tap window ${th.double_tap_ms} ms` }),
        h('span', { 'data-k': 'dec', text: 'decision' }))),
      why));

  // ── geometry ──
  let padSize = 400;
  let pc, pw, ph;
  const padFit = fitCanvas(pad, ({ c, w, h: hh }) => { pc = c; pw = w; ph = hh; padSize = w; schedule(); });
  let sc, sw, sh;
  const scopeFit = fitCanvas(scope, ({ c, w, h: hh }) => { sc = c; sw = w; sh = hh; schedule(); });
  const k = () => padSize / W; // pad px per watch px

  function toWatch(ev, el) {
    const r = el.getBoundingClientRect();
    return [((ev.clientX - r.left) / r.width) * W, ((ev.clientY - r.top) / r.height) * W];
  }

  // ── input ──
  function feed(type, x, y, from = 'pad') {
    const t = performance.now();
    if (type === 'down') {
      S.down = true; S.x0 = S.x = x; S.y0 = S.y = y; S.tDown = t; S.moved = false; S.fired = false; S.dist = 0;
      S.gap = S.lastUp != null && S.tapAt != null ? Math.round(t - S.tapAt) : null;
      S.stroke = [{ x, y, t }];
      S.trail.push(S.stroke);
      classifier.touch(true, x, y, t);
    } else if (type === 'move' && S.down) {
      S.x = x; S.y = y;
      S.dist = Math.max(Math.abs(x - S.x0), Math.abs(y - S.y0));
      if (S.dist > th.tap_slop_px) S.moved = true;
      S.stroke.push({ x, y, t });
      classifier.touch(true, x, y, t);
    } else if (type === 'up' && S.down) {
      S.down = false; S.tUp = t; S.lastUp = t; S.lastHold = Math.round(t - S.tDown);
      classifier.touch(false, S.x, S.y, t);
      if (S.moved && !S.fired) verdict('No gesture', 'no', t, S.dist < th.swipe_min_px ? `moved ${Math.round(S.dist)} px: too short for a swipe, too far for a tap` : `took more than ${th.swipe_max_ms} ms`);
    }
    if (from === 'pad' || from === 'watch') {
      try { ctx.stage?.trace?.(S.stroke.slice(-48).map((p) => ({ x: p.x, y: p.y, t: p.t }))); } catch { /* stage optional */ }
    }
    schedule();
  }

  let capture = null;
  pad.addEventListener('pointerdown', (e) => {
    if (capture != null) return;
    capture = e.pointerId; pad.setPointerCapture(e.pointerId); e.preventDefault();
    const [x, y] = toWatch(e, pad); feed('down', x, y);
    if (mirror) ctx.watch?.pointer?.('down', x, y);
  });
  pad.addEventListener('pointermove', (e) => {
    if (e.pointerId !== capture) return;
    const [x, y] = toWatch(e, pad); feed('move', x, y);
    if (mirror) ctx.watch?.pointer?.('move', x, y);
  });
  const end = (e) => {
    if (e.pointerId !== capture) return;
    capture = null;
    const [x, y] = toWatch(e, pad); feed('up', x, y);
    if (mirror) ctx.watch?.pointer?.('up', x, y);
  };
  pad.addEventListener('pointerup', end);
  pad.addEventListener('pointercancel', end);
  pad.addEventListener('contextmenu', (e) => e.preventDefault());

  // The real watch on the stage is also a lab instrument.
  let watchCanvas = null;
  function attachWatch() {
    const c = document.querySelector('#stage canvas');
    if (!c || c === watchCanvas) return;
    watchCanvas = c;
    let id = null;
    c.addEventListener('pointerdown', (e) => { id = e.pointerId; const [x, y] = toWatch(e, c); feed('down', x, y, 'watch'); });
    c.addEventListener('pointermove', (e) => { if (e.pointerId === id) { const [x, y] = toWatch(e, c); feed('move', x, y, 'watch'); } });
    const up = (e) => { if (e.pointerId === id) { id = null; const [x, y] = toWatch(e, c); feed('up', x, y, 'watch'); } };
    c.addEventListener('pointerup', up); c.addEventListener('pointercancel', up);
  }

  // ── frame loop (runs only while something is moving) ──
  function schedule() { if (!S.raf) S.raf = requestAnimationFrame(frame); }
  function frame() {
    S.raf = 0;
    const now = performance.now();
    classifier.tick(now);
    deferrer.tick(now, S.down);
    draw(now);
    const fading = S.trail.some((s) => now - s[s.length - 1].t < 1100);
    const windowOpen = S.lastUp != null && now - S.lastUp < th.tap_defer_ms + 700;
    const burst = S.events.length && now - S.lastEvent < 900;
    if (S.down || deferrer.pending || fading || windowOpen || burst) S.raf = requestAnimationFrame(frame);
  }

  function draw(now) {
    if (!pc) { const f = padFit(); pc = f.c; pw = f.w; ph = f.h; }
    if (!sc) { const f = scopeFit(); sc = f.c; sw = f.w; sh = f.h; }
    drawPad(now);
    drawScope(now);
    const hold = S.down ? now - S.tDown : S.lastHold;
    const sinceUp = S.lastUp != null && !S.down ? now - S.lastUp : 0;
    nDist.set(S.dist, S.dist / swipeMax, S.dist >= th.swipe_min_px);
    nHold.set(hold, hold / holdMax, hold >= th.long_press_ms);
    nWin.set(sinceUp, sinceUp / winMax, sinceUp > 0 && sinceUp <= th.double_tap_ms);
  }

  function drawPad(now) {
    const C = colors();
    const c = pc, w = pw, hh = ph, kk = k();
    c.clearRect(0, 0, w, hh);
    // the round face
    c.save();
    c.beginPath(); c.arc(w / 2, hh / 2, w / 2 - 1, 0, 7); c.clip();
    c.fillStyle = '#000'; c.fillRect(0, 0, w, hh);
    const g = c.createRadialGradient(w / 2, hh / 2, w * 0.1, w / 2, hh / 2, w * 0.5);
    g.addColorStop(0, 'rgba(120,160,255,0.05)'); g.addColorStop(1, 'rgba(0,0,0,0)');
    c.fillStyle = g; c.fillRect(0, 0, w, hh);
    c.restore();
    c.strokeStyle = 'rgba(230,237,243,0.16)'; c.lineWidth = 1.5;
    c.beginPath(); c.arc(w / 2, hh / 2, w / 2 - 1, 0, 7); c.stroke();

    if (!S.trail.length) {
      c.fillStyle = 'rgba(230,237,243,0.38)'; c.font = `500 ${Math.round(w * 0.04)}px ui-sans-serif, system-ui`; c.textAlign = 'center';
      c.fillText('touch here', w / 2, hh / 2 + 4);
      c.strokeStyle = 'rgba(230,237,243,0.08)'; c.setLineDash([2, 7]);
      c.beginPath(); c.arc(w / 2, hh / 2, th.tap_slop_px * kk * 1.8, 0, 7); c.stroke(); c.setLineDash([]);
    }

    // light-painting trails
    c.save(); c.globalCompositeOperation = 'lighter'; c.lineCap = 'round'; c.lineJoin = 'round';
    S.trail = S.trail.filter((s) => now - s[s.length - 1].t < 1300);
    for (const s of S.trail) {
      for (let i = 1; i < s.length; i++) {
        const age = now - s[i].t;
        const a = clamp(1 - age / 1200, 0, 1);
        if (a <= 0) continue;
        c.strokeStyle = hexA(C.accent, a * 0.9);
        c.shadowColor = C.accent; c.shadowBlur = 16 * a; c.lineWidth = 2 + 5 * a;
        c.beginPath(); c.moveTo(s[i - 1].x * kk, s[i - 1].y * kk); c.lineTo(s[i].x * kk, s[i].y * kk); c.stroke();
      }
    }
    c.restore();

    // classifier overlays
    if (S.down) {
      const ox = S.x0 * kk, oy = S.y0 * kk, r = th.tap_slop_px * kk, sr = th.swipe_min_px * kk;
      // slop zone: the C code tests |dx|,|dy| > 18 per axis, so it is a rounded square, not a circle
      const grow = clamp((now - S.tDown) / 140, 0, 1);
      const rr = r * (0.4 + 0.6 * grow);
      c.lineWidth = 1.5; c.strokeStyle = S.moved ? hexA(C.amber, 0.9) : hexA(C.text, 0.75);
      c.shadowColor = S.moved ? C.amber : C.accent; c.shadowBlur = 8;
      roundRect(c, ox - rr, oy - rr, rr * 2, rr * 2, rr * 0.55); c.stroke(); c.shadowBlur = 0;
      // swipe distance threshold
      c.setLineDash([3, 6]); c.strokeStyle = hexA(S.dist >= th.swipe_min_px ? C.success : C.text, S.dist >= th.swipe_min_px ? 0.8 : 0.25);
      c.beginPath(); c.arc(ox, oy, sr, 0, 7); c.stroke(); c.setLineDash([]);
      if (S.moved) {
        c.strokeStyle = hexA(C.accent, 0.5); c.lineWidth = 1;
        c.beginPath(); c.moveTo(ox, oy); c.lineTo(S.x * kk, S.y * kk); c.stroke();
      }
      // long-press ring filling to long_press_ms (only while it can still fire)
      if (!S.moved) {
        const f = clamp((now - S.tDown) / th.long_press_ms, 0, 1);
        const lr = r * 2.1;
        c.lineWidth = 3; c.strokeStyle = 'rgba(230,237,243,0.12)';
        c.beginPath(); c.arc(ox, oy, lr, 0, 7); c.stroke();
        c.strokeStyle = f >= 1 ? C.success : C.amber; c.shadowColor = c.strokeStyle; c.shadowBlur = f >= 1 ? 18 : 8;
        c.beginPath(); c.arc(ox, oy, lr, -Math.PI / 2, -Math.PI / 2 + f * Math.PI * 2); c.stroke(); c.shadowBlur = 0;
      }
      // finger bead
      c.fillStyle = '#fff'; c.shadowColor = C.accent; c.shadowBlur = 18;
      c.beginPath(); c.arc(S.x * kk, S.y * kk, 4, 0, 7); c.fill(); c.shadowBlur = 0;
    } else if (S.tapAt != null && S.lastUp != null) {
      // double-tap window and tap deferral, counting down at the last tap
      const el = now - S.tapAt;
      const ox = S.x0 * kk, oy = S.y0 * kk;
      const rW = th.tap_slop_px * kk * 2.6;
      if (el < th.tap_defer_ms && !S.firedAt && !(S.events.at(-1)?.kind === 'double')) {
        c.lineWidth = 3; c.lineCap = 'round';
        c.strokeStyle = 'rgba(210,153,34,0.14)'; c.beginPath(); c.arc(ox, oy, rW, 0, 7); c.stroke();
        const fw = clamp(el / th.double_tap_ms, 0, 1);
        c.strokeStyle = C.amber; c.shadowColor = C.amber; c.shadowBlur = 10;
        c.beginPath(); c.arc(ox, oy, rW, -Math.PI / 2 + fw * Math.PI * 2, -Math.PI / 2 + Math.PI * 2); c.stroke();
        c.shadowBlur = 0;
        if (screen.defers && el > th.double_tap_ms) {
          c.strokeStyle = hexA(C.text, 0.8); c.setLineDash([2, 5]);
          c.beginPath(); c.arc(ox, oy, rW + 7, 0, 7); c.stroke(); c.setLineDash([]);
        }
      }
    }
  }

  function roundRect(c, x, y, w, hgt, r) {
    c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + hgt, r); c.arcTo(x + w, y + hgt, x, y + hgt, r);
    c.arcTo(x, y + hgt, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath();
  }

  // ── oscilloscope ──
  const SPAN = 2200; // ms across the screen
  function drawScope(now) {
    const C = colors();
    const c = sc, w = sw, hh = sh;
    c.clearRect(0, 0, w, hh);
    const padL = 14, padR = 14, top = 34, base = hh - 40, hi = top + 34;
    const live = S.down || (S.events.length && now - S.lastEvent < 900) || deferrer.pending;
    const end = S.events.length ? (live ? now : S.lastEvent + 500) : now;
    const start = Math.max(S.events.length ? S.burstStart - 150 : end - SPAN, end - SPAN);
    const X = (t) => padL + ((t - start) / SPAN) * (w - padL - padR);

    // graticule
    c.lineWidth = 1;
    for (let ms = Math.ceil(start / 100) * 100; ms < start + SPAN; ms += 100) {
      const major = ms % 500 === 0;
      c.strokeStyle = major ? 'rgba(230,237,243,0.10)' : 'rgba(230,237,243,0.04)';
      c.beginPath(); c.moveTo(X(ms), top - 18); c.lineTo(X(ms), base + 8); c.stroke();
    }
    c.strokeStyle = 'rgba(230,237,243,0.10)';
    c.beginPath(); c.moveTo(padL, base); c.lineTo(w - padR, base); c.stroke();

    if (!S.events.length) {
      c.fillStyle = 'rgba(230,237,243,0.35)'; c.font = '500 13px ui-monospace, Menlo, monospace'; c.textAlign = 'center';
      c.fillText('waiting for a touch', w / 2, (top + base) / 2); return;
    }

    const t0 = S.burstStart;
    c.font = '500 11px ui-monospace, Menlo, monospace'; c.textAlign = 'left';
    // time labels relative to the first press
    for (let ms = Math.ceil((start - t0) / 500) * 500; t0 + ms < start + SPAN; ms += 500) {
      if (ms < 0) continue;
      c.fillStyle = 'rgba(230,237,243,0.35)'; c.fillText(ms + ' ms', X(t0 + ms) + 3, base + 22);
    }

    // double-tap window (and the 30 ms slack that makes the 330 ms deferral)
    for (const e of S.events) {
      if (e.kind !== 'tap') continue;
      const a = X(e.t), b = X(e.t + th.double_tap_ms), d = X(e.t + th.tap_defer_ms);
      c.fillStyle = hexA(C.amber, 0.10); c.fillRect(a, top, b - a, base - top);
      c.fillStyle = hexA(C.amber, 0.22); c.fillRect(b, top, d - b, base - top);
      c.strokeStyle = hexA(C.amber, 0.55); c.setLineDash([3, 4]); c.beginPath(); c.moveTo(b, top); c.lineTo(b, base); c.stroke(); c.setLineDash([]);
      c.fillStyle = hexA(C.amber, 0.9); c.fillText(`${th.double_tap_ms} ms`, Math.max(a + 4, b - 46), top + 12);
      if (d - b > 14) c.fillText(`${th.tap_defer_ms}`, d - 24, top + 12);
    }

    // signal: a stepped trace of "finger down"
    const segs = [];
    let cur = null;
    for (const e of S.events) {
      if (e.kind === 'press') cur = { a: e.t, b: null };
      else if (e.kind === 'release' && cur) { cur.b = e.t; segs.push(cur); cur = null; }
    }
    if (cur) segs.push({ a: cur.a, b: now });
    const path = () => {
      c.beginPath(); c.moveTo(padL, base);
      for (const s of segs) {
        c.lineTo(X(s.a), base); c.lineTo(X(s.a), hi); c.lineTo(X(s.b), hi); c.lineTo(X(s.b), base);
      }
      c.lineTo(w - padR, base);
    };
    c.save(); c.lineJoin = 'round';
    c.strokeStyle = hexA(C.accent, 0.35); c.lineWidth = 7; c.shadowColor = C.accent; c.shadowBlur = 18; path(); c.stroke();
    c.strokeStyle = '#dce9ff'; c.lineWidth = 1.8; c.shadowBlur = 4; path(); c.stroke();
    c.restore();

    // hold durations above each pulse; long-press threshold tick
    c.textAlign = 'center';
    for (const s of segs) {
      const mid = (X(s.a) + X(s.b)) / 2;
      c.fillStyle = 'rgba(230,237,243,0.75)';
      c.fillText(Math.round(s.b - s.a) + ' ms', mid, hi - 8);
      if (s.b - s.a > 40 && !S.events.some((e) => e.kind === 'long')) {
        // faint tick where a long press would fire
        const lx = X(s.a + th.long_press_ms);
        if (lx < w - padR && (s.b === now || s.b - s.a < th.long_press_ms)) {
          c.strokeStyle = 'rgba(230,237,243,0.18)'; c.setLineDash([1, 4]);
          c.beginPath(); c.moveTo(lx, hi); c.lineTo(lx, base); c.stroke(); c.setLineDash([]);
          c.fillStyle = 'rgba(230,237,243,0.35)'; c.fillText(`${th.long_press_ms}`, lx, hi - 8);
        }
      }
    }

    // decision markers
    const decisions = S.events.filter((e) => ['tap', 'fire', 'double', 'long', 'swipe'].includes(e.kind));
    decisions.forEach((e, i) => {
      const x = X(e.t);
      if (x < padL - 2 || x > w - padR) return;
      const col = e.kind === 'fire' ? C.success : e.kind === 'tap' ? C.text : e.kind === 'long' ? C.amber : C.success;
      c.strokeStyle = hexA(col, 0.9); c.lineWidth = 1.4; c.setLineDash(e.kind === 'tap' ? [2, 3] : []);
      c.beginPath(); c.moveTo(x, top - 4); c.lineTo(x, base); c.stroke(); c.setLineDash([]);
      c.fillStyle = col; c.shadowColor = col; c.shadowBlur = 8;
      c.beginPath(); c.arc(x, top - 8, 3.2, 0, 7); c.fill(); c.shadowBlur = 0;
      c.textAlign = x > w - 120 ? 'right' : 'left';
      c.fillStyle = col;
      c.fillText(e.label, x + (c.textAlign === 'left' ? 7 : -7), top - 5 - (i % 2) * 0);
    });
    // time between decisions
    const tp = S.events.find((e) => e.kind === 'tap'), fi = S.events.find((e) => e.kind === 'fire' && e.label === 'tap fires');
    if (tp && fi) {
      const y = base - 12;
      c.strokeStyle = 'rgba(230,237,243,0.5)'; c.lineWidth = 1;
      c.beginPath(); c.moveTo(X(tp.t), y); c.lineTo(X(fi.t), y); c.stroke();
      c.fillStyle = '#fff'; c.textAlign = 'center'; c.font = '600 12px ui-monospace, Menlo, monospace';
      c.fillText(`+${Math.round(fi.t - tp.t)} ms`, (X(tp.t) + X(fi.t)) / 2, y - 6);
    }
  }

  // exposed for the gesture matrix: demos also appear on the scope
  return {
    feed, attachWatch, redraw: () => schedule(),
    setScreen(id) { const s = SCREENS.find((x) => x.id === id); if (s) screenBtns[SCREENS.indexOf(s)].click(); },
    resize() { padFit(); scopeFit(); schedule(); },
  };
}
