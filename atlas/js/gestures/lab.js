// The gesture lab: a practice ring + live classifier readouts + oscilloscope timing diagram.
// Everything is driven by classify.js (a JS mirror of gesture.c: tap, swipe, long press and the
// rim-scrub recogniser) using the thresholds in facts.gesture. What a gesture DOES for the selected
// session comes from actions.js (the per-state defaults of the Ring UI, UI-RING.md).
import { createClassifier, angleDeg } from './classify.js';
import { STATE_ROWS, pillFor, rowColor, tone } from './actions.js';
import { h, fitCanvas, hexA, clamp, cssVar } from './dom.js';

const GLYPH = { swipe_u: '↑', swipe_d: '↓', swipe_l: '←', swipe_r: '→' };
const NAME = { swipe_u: 'Swipe up', swipe_d: 'Swipe down', swipe_l: 'Swipe left', swipe_r: 'Swipe right' };
const SAMPLE = {
  work: ['copilot', 'api'], ask: ['claude', 'docs'], perm: ['claude', 'webapp'],
  done: ['copilot', 'cli'], error: ['copilot', 'infra'], idle: ['claude', 'blog'],
};
const tri = (ms, period) => { const p = (ms % period) / period; return p < 0.5 ? p * 2 : 2 - p * 2; };
const rad = (deg) => ((deg - 90) * Math.PI) / 180;

export function createLab(root, ctx) {
  const f = ctx.facts, th = f.gesture, ring = f.ring;
  const W = f.display.width;
  const N = STATE_ROWS.length; // one ring segment per state, so every behaviour can be tried
  const colors = () => ({
    accent: cssVar('--fw-accent', f.palette.accent),
    amber: cssVar('--fw-amber', f.palette.amber),
    success: cssVar('--fw-success', f.palette.success),
    muted: cssVar('--fw-muted', f.palette.muted),
    text: cssVar('--fw-text', f.palette.text),
    danger: cssVar('--fw-danger', f.palette.danger),
  });

  // ── state ──
  let sel = STATE_ROWS.findIndex((r) => r.id === 'perm');
  let mirror = false;
  const S = {
    down: false, x0: 0, y0: 0, x: 0, y: 0, tDown: 0, tUp: 0, lastUp: null, lastHold: 0,
    trail: [], stroke: [], dist: 0, moved: false, fired: false, rim: false, startDeg: 0,
    events: [], burstStart: 0, lastEvent: 0, raf: 0, visible: false,
    pressCentre: false, holding: false, holdT0: 0, holdFired: false, rampOn: false, flashAt: 0,
  };
  const row = () => STATE_ROWS[sel];

  const log = (kind, label, t) => {
    if (!S.events.length || t - S.lastEvent > 3500) { S.events = []; S.burstStart = t; }
    S.events.push({ kind, label, t });
    S.lastEvent = t;
  };

  const verdict = (text, tn, sub = '') => {
    chip.dataset.tone = tn;
    chipText.textContent = text;
    chipSub.textContent = sub;
    chip.classList.remove('is-pop'); void chip.offsetWidth; chip.classList.add('is-pop');
  };

  // A visual haptic: the stage pulses in time with the pattern's pulses.
  const buzz = (name) => {
    const steps = f.haptics[name];
    if (!steps) return;
    try { ctx.stage?.pulse?.('haptic'); } catch { /* optional */ }
    let t = 0;
    steps.slice(1).forEach((s0, i) => {
      t += steps[i].on_ms + steps[i].off_ms;
      setTimeout(() => { try { ctx.stage?.pulse?.('haptic'); } catch { /* optional */ } }, t);
    });
    ctx.bus?.emit?.('watch:haptic', { steps, from: 'atlas' });
  };

  function select(i, t, why) {
    if (i === sel) return false;
    sel = i;
    S.holding = false; S.holdFired = false; S.rampOn = false;
    S.flashAt = t;
    syncPicker();
    log(why === 'scrub' ? 'detent' : 'select', why === 'scrub' ? 'detent' : 'select', t);
    return true;
  }
  const sectorAtDeg = (deg) => Math.min(N - 1, Math.trunc((deg * N) / 360));

  // mirrors ui_home_gesture()
  function onGesture(e, t) {
    const r = row();
    switch (e.kind) {
      case 'press':
        S.pressCentre = angleDeg(th, e.x, e.y).r < th.centre_r_px;
        S.holdFired = false;
        log('press', 'press', t);
        if (S.pressCentre && r.id === 'perm') { S.holding = true; S.holdT0 = t; S.rampOn = false; }
        break;
      case 'release': log('release', 'release', t); cancelHold(t); break;
      case 'tap': {
        const a = angleDeg(th, e.x, e.y);
        if (a.r < th.centre_r_px) {
          const c = r.cells.tap;
          log('fire', 'act', t);
          verdict('Tap · centre', tone(c), c.text);
        } else {
          log('tap', 'tap', t);
          const i = sectorAtDeg(a.deg);
          select(i, t, 'tap');
          verdict('Tap · segment', 'ok', `selects ${STATE_ROWS[i].label}; no action`);
        }
        break;
      }
      case 'long_press': {
        if (!S.pressCentre) break;
        const c = r.cells.long_press;
        log('long', 'long press', t);
        verdict('Long-press', tone(c), c.text);
        break;
      }
      case 'swipe_d':
        log('swipe', 'swipe ↓', t);
        if (S.pressCentre && r.id === 'perm') { cancelHold(t, true); verdict('Swipe down', 'danger', r.cells.swipe_d.text); buzz('denied'); }
        else verdict('Swipe down', 'no', r.cells.swipe_d.text);
        break;
      case 'swipe_u': case 'swipe_l': case 'swipe_r':
        log('swipe', 'swipe ' + GLYPH[e.kind], t);
        verdict(NAME[e.kind], 'no', 'No action: the only swipe on the ring is down, to deny');
        break;
      case 'scrub_start':
        cancelHold(t, true);
        log('scrub', 'scrub', t);
        if (select(e.x, t, 'scrub')) buzz('detent');
        verdict('Scrub', 'ok', `along the rim: ${row().label} selected`);
        break;
      case 'scrub':
        if (select(e.x, t, 'scrub')) buzz('detent');
        verdict('Scrub', 'ok', `${row().label} · ${e.dur_ms}° swept`);
        break;
      case 'scrub_end': log('scrub', 'scrub end', t); S.fired = true; break;
      default: break;
    }
  }
  function cancelHold(t, silent) {
    if (!S.holding) return;
    const el = t - S.holdT0;
    S.holding = false;
    S.rampOn = false;
    if (!silent && el >= th.hold_show_ms) verdict('Hold cancelled', 'no', `released at ${Math.round(el)} ms; ${th.hold_confirm_ms} ms needed`);
  }
  function holdTick(now) {
    if (!S.holding || S.holdFired) return;
    const el = now - S.holdT0;
    if (el >= th.hold_show_ms && !S.rampOn) { S.rampOn = true; buzz('hold_ramp'); }
    if (S.rampOn && el < th.hold_confirm_ms) verdict('Holding…', 'wait', `${Math.round(el)} of ${th.hold_confirm_ms} ms`);
    if (el >= th.hold_confirm_ms) {
      S.holdFired = true; S.holding = false; S.rampOn = false; S.flashAt = now;
      log('fire', 'approve', now);
      verdict('Approved', 'ok', `held ${Math.round(el)} ms. ${row().cells.hold.text}`);
      buzz('approved');
    }
  }

  const classifier = createClassifier((e) => onGesture(e, performance.now()), th);
  classifier.setSectors(N);

  // ── DOM ──
  const pad = h('canvas', {
    class: 'lab__pad', role: 'application', tabindex: 0,
    'aria-label': 'Practice ring. Drag around the rim to select a session, tap the centre to act, hold the centre to approve, swipe down to deny.',
  });
  const chipText = h('b', { class: 'chip__text', text: 'Try it' });
  const chipSub = h('span', { class: 'chip__sub', text: 'drag round the rim, tap or hold the centre' });
  const chip = h('div', { class: 'chip', 'data-tone': 'idle', 'aria-live': 'polite' }, chipText, chipSub);

  const num = (label, unit, limits) => {
    const v = h('b', { class: 'num__v', text: '0' });
    const bar = h('i', { class: 'num__bar' }, h('u', { class: 'num__fill' }));
    for (const l of limits) bar.append(h('s', { class: 'num__mark', style: { left: l.pos + '%' }, title: l.label }));
    const el = h('div', { class: 'num' },
      h('div', { class: 'num__top' }, h('span', { class: 'num__l', text: label }), h('span', { class: 'num__u', text: unit })), v, bar);
    return { el, set(val, frac, hot) {
      v.textContent = String(Math.round(val));
      bar.firstChild.style.transform = `scaleX(${clamp(frac, 0, 1)})`;
      el.dataset.hot = hot ? '1' : '';
    } };
  };
  const arcMax = th.scrub_start_deg * 4, holdMax = th.hold_confirm_ms * 1.1, swipeMax = th.swipe_min_px * 1.6;
  const nArc = num('rim arc', '°', [{ pos: (th.scrub_start_deg / arcMax) * 100, label: `scrub starts at ${th.scrub_start_deg}°` }]);
  const nHold = num('hold', 'ms', [
    { pos: (th.long_press_ms / holdMax) * 100, label: `long press ${th.long_press_ms} ms` },
    { pos: (th.hold_confirm_ms / holdMax) * 100, label: `approve ${th.hold_confirm_ms} ms` }]);
  const nDist = num('distance', 'px', [{ pos: (th.swipe_min_px / swipeMax) * 100, label: `swipe ≥ ${th.swipe_min_px} px` }]);

  const pickBtns = STATE_ROWS.map((r, i) => h('button', {
    type: 'button', class: 'pick__b', 'aria-pressed': String(i === sel), title: `Select a ${r.label} session`,
    style: { '--st': rowColor(r, f) }, onclick: () => { select(i, performance.now(), 'tap'); schedule(); },
  }, h('i', { class: 'pick__dot' }), h('span', { text: r.label })));
  function syncPicker() { pickBtns.forEach((b, i) => b.setAttribute('aria-pressed', String(i === sel))); }
  const pickNote = h('p', { class: 'lab__note', text: 'Six sessions, one per state. Scrub the rim, or pick one here, then act on it.' });
  const mirrorBtn = h('button', { type: 'button', class: 'toggle', 'aria-pressed': 'false', onclick: () => {
    mirror = !mirror; mirrorBtn.setAttribute('aria-pressed', String(mirror));
  } }, h('i', { class: 'toggle__knob' }), h('span', { text: 'Also send to the watch' }));

  const scope = h('canvas', { class: 'scope', 'aria-label': 'Timing diagram of your touches and the decisions the classifier makes', role: 'img' });

  const why = h('p', { class: 'lab__why' },
    h('b', { text: 'No waiting. ' }),
    `A tap acts the moment you lift. Approving is the one irreversible action, so it can’t be a tap: the arc starts filling after ${th.hold_show_ms} ms ` +
    `(a casual touch never flashes it) and the approval is sent at ${th.hold_confirm_ms} ms. Let go early and nothing happens.`);

  root.append(
    h('div', { class: 'lab' },
      h('div', { class: 'lab__main' },
        h('div', { class: 'lab__padwrap' }, pad),
        h('div', { class: 'lab__side' },
          chip,
          h('div', { class: 'nums' }, nArc.el, nHold.el, nDist.el),
          h('div', { class: 'lab__ctl' },
            h('span', { class: 'lab__ctl-l', text: 'Selected session' }),
            h('div', { class: 'pick', role: 'group', 'aria-label': 'Selected session' }, pickBtns),
            pickNote, mirrorBtn))),
      h('div', { class: 'scopewrap' }, scope, h('div', { class: 'scope__legend' },
        h('span', { 'data-k': 'sig', text: 'finger' }),
        h('span', { 'data-k': 'win', text: 'threshold' }),
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
  function feed(type, x, y) {
    const t = performance.now();
    if (type === 'down') {
      S.down = true; S.x0 = S.x = x; S.y0 = S.y = y; S.tDown = t; S.moved = false; S.fired = false; S.dist = 0;
      const a = angleDeg(th, x, y);
      S.rim = a.r >= th.rim_in_px && a.r <= th.rim_out_px;
      S.startDeg = a.deg;
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
      if (S.moved && !S.fired && !S.holdFired && !classifier.scrubbing) {
        const swiped = S.events.some((e) => e.kind === 'swipe' && e.t >= t - 5);
        if (!swiped) verdict('No gesture', 'no', S.dist < th.swipe_min_px ? `moved ${Math.round(S.dist)} px: too short for a swipe, too far for a tap` : `took more than ${th.swipe_max_ms} ms`);
      }
    }
    try { ctx.stage?.trace?.(S.stroke.slice(-48).map((p) => ({ x: p.x, y: p.y, t: p.t }))); } catch { /* stage optional */ }
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

  // ── frame loop: runs while something moves, and continuously while the ring is on screen (it breathes) ──
  function schedule() { if (!S.raf) S.raf = requestAnimationFrame(frame); }
  function frame() {
    S.raf = 0;
    const now = performance.now();
    classifier.tick(now);
    holdTick(now);
    draw(now);
    const fading = S.trail.some((s) => now - s[s.length - 1].t < 1100);
    const burst = S.events.length && now - S.lastEvent < 900;
    if (S.down || fading || burst || (S.visible && !ctx.reducedMotion)) S.raf = requestAnimationFrame(frame);
  }
  if (typeof IntersectionObserver !== 'undefined') {
    new IntersectionObserver((es) => { S.visible = es.some((e) => e.isIntersecting); if (S.visible) schedule(); }).observe(pad);
  }

  function draw(now) {
    if (!pc) { const fi = padFit(); pc = fi.c; pw = fi.w; ph = fi.h; }
    if (!sc) { const fi = scopeFit(); sc = fi.c; sw = fi.w; sh = fi.h; }
    drawPad(now);
    drawScope(now);
    const holdMs = S.holding ? now - S.holdT0 : S.down ? now - S.tDown : S.lastHold;
    const g = classifier.state;
    nArc.set(Math.abs(g.arcDeg), Math.abs(g.arcDeg) / arcMax, classifier.scrubbing);
    nHold.set(holdMs, holdMs / holdMax, holdMs >= th.long_press_ms);
    nDist.set(S.dist, S.dist / swipeMax, S.dist >= th.swipe_min_px);
  }

  // ── the ring, as the firmware draws it (ui_ring.c shapes) ──
  function drawRing(c, w, now, kk, C) {
    const cx = w / 2, cy = w / 2;
    const gap = ring.seg_gap_deg / 2;
    c.lineCap = 'round'; // capsules
    STATE_ROWS.forEach((r, i) => {
      const col = rowColor(r, f);
      const a0 = (i * 360) / N + gap, a1 = ((i + 1) * 360) / N - gap, span = a1 - a0;
      const on = i === sel;
      const wd = on ? ring.sel_w_px : ring.band_w_px;
      const rr = ((on ? ring.sel_out_px : ring.band_out_px) - wd / 2) * kk;
      const arc = (d0, d1, radius, width, alpha, dash) => {
        c.strokeStyle = hexA(col, clamp(alpha, 0, 1));
        c.lineWidth = width * kk;
        c.setLineDash(dash || []);
        c.beginPath(); c.arc(cx, cy, radius, rad(d0), rad(d1)); c.stroke();
      };
      const anim = ctx.reducedMotion ? 0 : now;
      // UI-V3 §3.1: selected and needs-you segments at full brightness, the rest at 40 %; a pulse for
      // ask / perm, one comet for working; a pip (10 px, r 205) marks an unselected segment that needs you
      const needs = r.kind === 'ask' || r.kind === 'perm' || r.kind === 'error';
      const rest = on || needs ? 1 : ring.others_pct / 100;
      switch (r.kind) {
        case 'work': {
          arc(a0, a1, rr, wd, on ? 0.6 : rest);
          const p = a0 + ((anim % ring.work_period_ms) / ring.work_period_ms) * (span - 14);
          arc(p, p + 14, rr, wd, 1);
          break;
        }
        case 'ask': arc(a0, a1, rr, wd, 0.55 + 0.45 * tri(anim, ring.ask_period_ms)); break;
        case 'perm': arc(a0, a1, rr, wd, 0.55 + 0.45 * tri(anim, ring.perm_period_ms)); break;
        default: arc(a0, a1, rr, wd, rest);
      }
      if (needs && !on) {
        const m = rad((a0 + a1) / 2);
        c.fillStyle = hexA(col, 1);
        c.beginPath(); c.arc(cx + 205 * kk * Math.cos(m), cy + 205 * kk * Math.sin(m), 5 * kk, 0, 7); c.fill();
      }
    });
  }

  function drawCentre(c, w, now, kk, C) {
    const cx = w / 2, cy = w / 2, r = row(), col = rowColor(r, f);
    const [agent, repo] = SAMPLE[r.kind];
    const flash = clamp(1 - (now - S.flashAt) / 500, 0, 1);
    if (flash > 0) {
      const g = c.createRadialGradient(cx, cy, 20 * kk, cx, cy, 150 * kk);
      g.addColorStop(0, hexA(col, 0.22 * flash)); g.addColorStop(1, hexA(col, 0));
      c.fillStyle = g; c.beginPath(); c.arc(cx, cy, 150 * kk, 0, 7); c.fill();
    }
    c.textAlign = 'center'; c.textBaseline = 'middle';
    const font = (px, wgt = 500) => `${wgt} ${Math.max(8, Math.round(px * kk))}px ui-sans-serif, system-ui, sans-serif`;
    c.font = font(17); c.fillStyle = C.muted; c.fillText(agent, cx, cy - 86 * kk);
    c.font = font(34, 600); c.fillStyle = C.text; c.fillText(repo, cx, cy - 54 * kk);
    c.font = font(25); c.fillStyle = col; c.fillText(`${r.icon}  ${r.label}`, cx, cy - 12 * kk);
    // default-action pill
    const pill = pillFor(r, f);
    c.font = font(21, 600);
    const pwid = c.measureText(pill).width + 34 * kk, ph2 = 40 * kk, py = cy + 52 * kk;
    c.strokeStyle = col; c.lineWidth = 2 * kk; c.fillStyle = hexA(col, 0.12);
    c.beginPath(); c.roundRect(cx - pwid / 2, py - ph2 / 2, pwid, ph2, ph2 / 2); c.fill(); c.stroke();
    c.fillStyle = col; c.fillText(pill, cx, py + 1);
    if (r.id === 'perm') { c.font = font(16); c.fillStyle = C.muted; c.fillText('long swipe down to deny', cx, cy + 96 * kk); }
    // edge hints beside the buttons
    const hint = (deg, text, colr) => {
      const x = cx + ring.hint_r_px * kk * Math.sin((deg * Math.PI) / 180), y = cy - ring.hint_r_px * kk * Math.cos((deg * Math.PI) / 180);
      c.font = font(14, 600); c.fillStyle = colr; c.fillText(text, x, y);
    };
    hint(ring.hint_a_deg, ring.edge_hints.a, col);
    hint(ring.hint_b_deg, ring.edge_hints.b, C.muted);
  }

  function drawPad(now) {
    const C = colors();
    const c = pc, w = pw, hh = ph, kk = k();
    const cx = w / 2, cy = hh / 2;
    c.clearRect(0, 0, w, hh);
    c.save();
    c.beginPath(); c.arc(cx, cy, w / 2 - 1, 0, 7); c.clip();
    c.fillStyle = '#000'; c.fillRect(0, 0, w, hh);
    c.restore();
    c.strokeStyle = 'rgba(230,237,243,0.16)'; c.lineWidth = 1.5;
    c.beginPath(); c.arc(cx, cy, w / 2 - 1, 0, 7); c.stroke();

    // the rim annulus: where a drag counts as a scrub; faint until you touch it
    const rimOn = S.down && S.rim;
    c.lineWidth = (th.rim_out_px - th.rim_in_px) * kk;
    c.strokeStyle = `rgba(230,237,243,${rimOn ? 0.07 : 0.025})`;
    c.beginPath(); c.arc(cx, cy, ((th.rim_in_px + th.rim_out_px) / 2) * kk, 0, 7); c.stroke();
    // sector detents
    c.lineWidth = 1; c.strokeStyle = 'rgba(230,237,243,0.12)';
    for (let i = 0; i < N; i++) {
      const a = rad((i * 360) / N);
      c.beginPath();
      c.moveTo(cx + Math.cos(a) * th.rim_in_px * kk, cy + Math.sin(a) * th.rim_in_px * kk);
      c.lineTo(cx + Math.cos(a) * th.rim_out_px * kk, cy + Math.sin(a) * th.rim_out_px * kk);
      c.stroke();
    }

    drawRing(c, w, now, kk, C);
    drawCentre(c, w, now, kk, C);

    // hold-to-approve arc, filling to hold_confirm_ms
    if (S.holding && now - S.holdT0 >= th.hold_show_ms) {
      const fr = clamp((now - S.holdT0) / th.hold_confirm_ms, 0, 1);
      c.lineCap = 'round'; c.lineWidth = ring.prog_w_px * kk * 1.4;
      c.strokeStyle = 'rgba(255,255,255,0.12)'; c.beginPath(); c.arc(cx, cy, ring.prog_out_px * kk - 12 * kk, 0, 7); c.stroke();
      c.strokeStyle = '#fff'; c.shadowColor = rowColor(row(), f); c.shadowBlur = 14;
      c.beginPath(); c.arc(cx, cy, ring.prog_out_px * kk - 12 * kk, rad(0), rad(fr * 360)); c.stroke(); c.shadowBlur = 0;
      c.fillStyle = '#fff'; c.font = `600 ${Math.round(13 * kk * 1.2)}px ui-monospace, Menlo, monospace`; c.textAlign = 'center';
      c.fillText(`${Math.round(fr * th.hold_confirm_ms)} ms`, cx, cy + 128 * kk);
    }

    // light-painting trails
    c.save(); c.globalCompositeOperation = 'lighter'; c.lineCap = 'round'; c.lineJoin = 'round';
    S.trail = S.trail.filter((s) => now - s[s.length - 1].t < 1300);
    for (const s of S.trail) {
      for (let i = 1; i < s.length; i++) {
        const a = clamp(1 - (now - s[i].t) / 1200, 0, 1);
        if (a <= 0) continue;
        c.strokeStyle = hexA(C.accent, a * 0.9);
        c.shadowColor = C.accent; c.shadowBlur = 16 * a; c.lineWidth = 2 + 5 * a;
        c.beginPath(); c.moveTo(s[i - 1].x * kk, s[i - 1].y * kk); c.lineTo(s[i].x * kk, s[i].y * kk); c.stroke();
      }
    }
    c.restore();

    if (S.down) drawOverlays(c, now, kk, C, cx, cy);
    else if (!S.trail.length) {
      c.fillStyle = 'rgba(230,237,243,0.34)'; c.font = `500 ${Math.round(12 * Math.max(kk, 0.8))}px ui-sans-serif, system-ui`; c.textAlign = 'center';
      c.fillText('drag the rim · tap · hold', cx, cy + 124 * kk);
    }
  }

  function drawOverlays(c, now, kk, C, cx, cy) {
    const ox = S.x0 * kk, oy = S.y0 * kk;
    if (S.rim) {
      // the arc swept along the rim, with the point where it becomes a scrub
      const g = classifier.state;
      const mid = ((th.rim_in_px + th.rim_out_px) / 2) * kk;
      const col = classifier.scrubbing ? C.success : C.amber;
      c.lineWidth = 3; c.lineCap = 'round'; c.strokeStyle = hexA(col, 0.95); c.shadowColor = col; c.shadowBlur = 10;
      c.beginPath(); c.arc(cx, cy, mid, rad(S.startDeg), rad(S.startDeg + g.arcDeg), g.arcDeg < 0); c.stroke(); c.shadowBlur = 0;
      for (const sgn of [1, -1]) {
        const a = rad(S.startDeg + sgn * th.scrub_start_deg);
        c.strokeStyle = hexA(C.text, 0.4); c.lineWidth = 1;
        c.beginPath(); c.moveTo(cx + Math.cos(a) * (mid - 9), cy + Math.sin(a) * (mid - 9)); c.lineTo(cx + Math.cos(a) * (mid + 9), cy + Math.sin(a) * (mid + 9)); c.stroke();
      }
    } else if (!S.holding) {
      const r = th.tap_slop_px * kk, sr = th.swipe_min_px * kk;
      const grow = clamp((now - S.tDown) / 140, 0, 1), rr = r * (0.4 + 0.6 * grow);
      c.lineWidth = 1.5; c.strokeStyle = S.moved ? hexA(C.amber, 0.9) : hexA(C.text, 0.75);
      c.shadowColor = S.moved ? C.amber : C.accent; c.shadowBlur = 8;
      c.beginPath(); c.roundRect(ox - rr, oy - rr, rr * 2, rr * 2, rr * 0.55); c.stroke(); c.shadowBlur = 0;
      c.setLineDash([3, 6]); c.strokeStyle = hexA(S.dist >= th.swipe_min_px ? C.success : C.text, S.dist >= th.swipe_min_px ? 0.8 : 0.25);
      c.beginPath(); c.arc(ox, oy, sr, 0, 7); c.stroke(); c.setLineDash([]);
      if (S.moved) {
        c.strokeStyle = hexA(C.accent, 0.5); c.lineWidth = 1;
        c.beginPath(); c.moveTo(ox, oy); c.lineTo(S.x * kk, S.y * kk); c.stroke();
      }
      if (!S.moved) {
        const fr = clamp((now - S.tDown) / th.long_press_ms, 0, 1), lr = r * 2.1;
        c.lineWidth = 3; c.strokeStyle = 'rgba(230,237,243,0.12)'; c.beginPath(); c.arc(ox, oy, lr, 0, 7); c.stroke();
        c.strokeStyle = fr >= 1 ? C.success : C.amber; c.shadowColor = c.strokeStyle; c.shadowBlur = fr >= 1 ? 18 : 8;
        c.beginPath(); c.arc(ox, oy, lr, -Math.PI / 2, -Math.PI / 2 + fr * Math.PI * 2); c.stroke(); c.shadowBlur = 0;
      }
    }
    c.fillStyle = '#fff'; c.shadowColor = C.accent; c.shadowBlur = 18;
    c.beginPath(); c.arc(S.x * kk, S.y * kk, 4, 0, 7); c.fill(); c.shadowBlur = 0;
  }

  // ── oscilloscope ──
  const SPAN = 2200; // ms across the screen
  function drawScope(now) {
    const C = colors();
    const c = sc, w = sw, hh = sh;
    c.clearRect(0, 0, w, hh);
    const padL = 14, padR = 14, top = 34, base = hh - 40, hi = top + 34;
    const live = S.down || (S.events.length && now - S.lastEvent < 900);
    const end = S.events.length ? (live ? now : S.lastEvent + 500) : now;
    const start = Math.max(S.events.length ? S.burstStart - 150 : end - SPAN, end - SPAN);
    const X = (t) => padL + ((t - start) / SPAN) * (w - padL - padR);

    c.lineWidth = 1;
    for (let ms = Math.ceil(start / 100) * 100; ms < start + SPAN; ms += 100) {
      c.strokeStyle = ms % 500 === 0 ? 'rgba(230,237,243,0.10)' : 'rgba(230,237,243,0.04)';
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
    for (let ms = Math.ceil((start - t0) / 500) * 500; t0 + ms < start + SPAN; ms += 500) {
      if (ms < 0) continue;
      c.fillStyle = 'rgba(230,237,243,0.35)'; c.fillText(ms + ' ms', X(t0 + ms) + 3, base + 22);
    }

    // finger-down segments
    const segs = [];
    let cur = null;
    for (const e of S.events) {
      if (e.kind === 'press') cur = { a: e.t, b: null };
      else if (e.kind === 'release' && cur) { cur.b = e.t; segs.push(cur); cur = null; }
    }
    if (cur) segs.push({ a: cur.a, b: now });

    // thresholds measured from the last press: arc appears, long press, approve
    const lastSeg = segs.at(-1);
    if (lastSeg) {
      const marks = [[th.long_press_ms, 'long press', C.amber]];
      if (row().id === 'perm') marks.unshift([th.hold_show_ms, 'arc', C.muted]), marks.push([th.hold_confirm_ms, 'approve', C.success]);
      for (const [ms, label, col] of marks) {
        const x = X(lastSeg.a + ms);
        if (x > w - padR || x < padL) continue;
        c.strokeStyle = hexA(col, 0.55); c.setLineDash([3, 4]);
        c.beginPath(); c.moveTo(x, top); c.lineTo(x, base); c.stroke(); c.setLineDash([]);
        c.fillStyle = hexA(col, 0.95); c.textAlign = 'center'; c.fillText(`${ms}`, x, base - 6);
        c.fillStyle = hexA(col, 0.7); c.fillText(label, x, top - 6);
      }
    }

    const path = () => {
      c.beginPath(); c.moveTo(padL, base);
      for (const s of segs) { c.lineTo(X(s.a), base); c.lineTo(X(s.a), hi); c.lineTo(X(s.b), hi); c.lineTo(X(s.b), base); }
      c.lineTo(w - padR, base);
    };
    c.save(); c.lineJoin = 'round';
    c.strokeStyle = hexA(C.accent, 0.35); c.lineWidth = 7; c.shadowColor = C.accent; c.shadowBlur = 18; path(); c.stroke();
    c.strokeStyle = '#dce9ff'; c.lineWidth = 1.8; c.shadowBlur = 4; path(); c.stroke();
    c.restore();

    c.textAlign = 'center'; c.fillStyle = 'rgba(230,237,243,0.75)';
    for (const s of segs) c.fillText(Math.round(s.b - s.a) + ' ms', (X(s.a) + X(s.b)) / 2, hi - 8);

    // detents: a click on the baseline each time the rim scrub crosses a segment
    for (const e of S.events) {
      if (e.kind !== 'detent' && e.kind !== 'select') continue;
      const x = X(e.t);
      c.fillStyle = C.accent; c.shadowColor = C.accent; c.shadowBlur = 8;
      c.beginPath(); c.moveTo(x, base); c.lineTo(x - 4, base - 12); c.lineTo(x + 4, base - 12); c.closePath(); c.fill(); c.shadowBlur = 0;
    }

    const decisions = S.events.filter((e) => ['tap', 'fire', 'long', 'swipe', 'scrub'].includes(e.kind));
    decisions.forEach((e) => {
      const x = X(e.t);
      if (x < padL - 2 || x > w - padR) return;
      const col = e.label === 'approve' || e.kind === 'fire' ? C.success : e.kind === 'tap' ? C.text : e.kind === 'long' ? C.amber : e.kind === 'scrub' ? C.accent : C.success;
      c.strokeStyle = hexA(col, 0.9); c.lineWidth = 1.4; c.setLineDash(e.kind === 'tap' ? [2, 3] : []);
      c.beginPath(); c.moveTo(x, top - 4); c.lineTo(x, base); c.stroke(); c.setLineDash([]);
      c.fillStyle = col; c.shadowColor = col; c.shadowBlur = 8;
      c.beginPath(); c.arc(x, top - 8, 3.2, 0, 7); c.fill(); c.shadowBlur = 0;
      c.textAlign = x > w - 120 ? 'right' : 'left'; c.fillStyle = col;
      c.fillText(e.label, x + (c.textAlign === 'left' ? 7 : -7), top - 14);
    });
  }

  return {
    feed, redraw: () => schedule(),
    attachWatch() { /* the pad is the instrument; the stage watch is driven separately */ },
    resize() { padFit(); scopeFit(); schedule(); },
  };
}
