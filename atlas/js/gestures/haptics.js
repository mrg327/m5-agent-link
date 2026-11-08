// Haptic seismograph: each pattern from facts.haptics drawn as the motor-duty waveform the
// firmware really produces (kick at 100% for 15 ms on pulses ≥ 30 ms, then the 85% hold; a pulse
// no longer than the kick, like the rim detent, is all kick).
import { h, svg, cssVar } from './dom.js';

const ORDER = [
  ['detent', 'Detent', 'accent'],
  ['hold_ramp', 'Approve ramp', 'amber'],
  ['approved', 'Approved', 'success'],
  ['denied', 'Denied', 'danger'],
  ['reject', 'Reject', 'danger'],
  ['permission', 'Permission', 'amber'],
  ['question', 'Question', 'amber'],
  ['done', 'Done', 'success'],
  ['error', 'Error', 'danger'],
  ['tick', 'Tick', null],
  ['hello', 'Hello', 'accent'],
];

// deterministic ripple so the trace looks like a real motor, and never changes between renders
function ripple(i) { return Math.sin(i * 12.9898) * 0.5 + Math.sin(i * 4.1414) * 0.5; }

export function createSeismograph(root, ctx) {
  const f = ctx.facts, motor = f.haptic_motor;
  const total = (steps) => steps.reduce((a, s) => a + s.on_ms + s.off_ms, 0);
  const VBW = 300, VBH = 64, base = 54, top = 8;
  // each card has its own time axis (at least 80 ms wide), so a 15 ms detent is still visible next to a 1.2 s ramp
  const axis = (steps) => { const span = Math.max(total(steps), 80); return (ms) => 6 + (ms / span) * (VBW - 12); };
  const Y = (duty) => base - (duty / 100) * (base - top);

  const cards = [];
  for (const [name, label, tone] of ORDER) {
    const steps = f.haptics[name];
    if (!steps) continue;
    const X = axis(steps);
    // path of the duty over time, with the kick
    let t = 0, d = `M ${X(0)} ${Y(0)}`;
    const marks = [];
    for (const s of steps) {
      const kick = s.kick_ms;
      d += ` L ${X(t)} ${Y(0)} L ${X(t)} ${Y(s.kick_duty)}`;
      if (kick) { d += ` L ${X(t + kick)} ${Y(s.kick_duty)} L ${X(t + kick)} ${Y(s.duty)}`; }
      d += ` L ${X(t + s.on_ms)} ${Y(s.duty)} L ${X(t + s.on_ms)} ${Y(0)}`;
      marks.push({ t, on: s.on_ms, kick });
      t += s.on_ms + s.off_ms;
      d += ` L ${X(t)} ${Y(0)}`;
    }
    // motor-noise ripple, drawn under the clean duty trace
    let rp = `M ${X(0)} ${Y(0)}`;
    for (const m of marks) {
      rp += ` M ${X(m.t)} ${Y(0)}`;
      for (let ms = m.t; ms <= m.t + m.on; ms += 3) {
        const duty = ms - m.t < m.kick || m.on <= motor.kick_ms ? motor.kick_duty : motor.hold_duty;
        rp += ` L ${X(ms)} ${Y(duty) + ripple(ms) * 3.2}`;
      }
      rp += ` L ${X(m.t + m.on)} ${Y(0)}`;
    }
    const cursor = svg('line', { class: 'seis__cursor', x1: X(0), x2: X(0), y1: top - 4, y2: base + 4 });
    const trace = svg('path', { class: 'seis__trace', d });
    const noise = svg('path', { class: 'seis__noise', d: rp });
    const fill = svg('path', { class: 'seis__fill', d: d + ` L ${X(t)} ${Y(0)} Z` });
    const w = svg('svg', { viewBox: `0 0 ${VBW} ${VBH}`, class: 'seis__svg', role: 'img', 'aria-hidden': 'true', preserveAspectRatio: 'none' },
      svg('line', { class: 'seis__base', x1: 6, x2: VBW - 6, y1: base, y2: base }), fill, noise, trace, cursor);
    const steps_ms = steps.map((s) => s.on_ms).join(' · ');
    const card = h('button', {
      type: 'button', class: 'seis', 'data-tone': tone || 'plain', 'aria-label': `Play ${label}: ${f.haptic_meaning[name]}`,
    },
      h('div', { class: 'seis__head' }, h('b', { class: 'seis__name', text: label }), h('span', { class: 'seis__ms mono', text: total(steps) + ' ms' })),
      w,
      h('p', { class: 'seis__meaning', text: f.haptic_meaning[name] }),
      h('p', { class: 'seis__steps mono', text: steps_ms + ' ms on' }));
    card.addEventListener('click', () => play(name, card, cursor, X, total(steps)));
    cards.push(card);
  }

  const legend = h('p', { class: 'seis__legend' },
    h('span', { class: 'seis__key mono', text: `${motor.kick_duty}% × ${motor.kick_ms} ms` }), ' kick to spin the motor up, then ',
    h('span', { class: 'seis__key mono', text: `${motor.hold_duty}%` }), ' hold. Only pulses of ',
    h('span', { class: 'mono', text: `≥ ${motor.kick_min_pulse_ms} ms` }), ' get the kick. The ',
    h('span', { class: 'mono', text: `${motor.kick_ms} ms` }), ' rim detent is all kick, rate-limited to one per ',
    h('span', { class: 'mono', text: `${motor.detent_min_gap_ms} ms` }), '.');
  const vib = typeof navigator !== 'undefined' && 'vibrate' in navigator;
  const note = h('p', { class: 'seis__hint', text: vib ? 'Click a pattern. Your phone will buzz it too.' : 'Click a pattern to see it on the watch.' });
  root.append(h('div', { class: 'seisgrid' }, cards), legend, note);

  let anim = 0;
  function play(name, card, cursor, X2, dur) {
    const steps = f.haptics[name];
    try { ctx.stage?.pulse?.('haptic'); } catch { /* optional */ }
    ctx.bus?.emit?.('watch:haptic', { steps, from: 'atlas' });
    if (vib) {
      const arr = [];
      steps.forEach((s) => { arr.push(s.on_ms); if (s.off_ms) arr.push(s.off_ms); });
      try { navigator.vibrate(arr); } catch { /* ignored */ }
    }
    // re-pulse the stage at each step start so the watch "buzzes" in time
    let t = 0;
    steps.slice(1).forEach((s0, i) => {
      t += steps[i].on_ms + steps[i].off_ms;
      setTimeout(() => { try { ctx.stage?.pulse?.('haptic'); } catch { /* optional */ } }, t);
    });
    card.classList.remove('is-playing'); void card.offsetWidth; card.classList.add('is-playing');
    cancelAnimationFrame(anim);
    if (ctx.reducedMotion) { cursor.setAttribute('x1', X2(dur)); cursor.setAttribute('x2', X2(dur)); setTimeout(() => card.classList.remove('is-playing'), dur + 300); return; }
    const t0 = performance.now();
    const step = (now) => {
      const ms = Math.min(dur, now - t0);
      cursor.setAttribute('x1', X2(ms)); cursor.setAttribute('x2', X2(ms));
      if (ms < dur) anim = requestAnimationFrame(step);
      else setTimeout(() => card.classList.remove('is-playing'), 300);
    };
    anim = requestAnimationFrame(step);
  }
}
