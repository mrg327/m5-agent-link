// Gesture → action matrix: rows are the states a session can be in, columns are the inputs.
// What each cell does comes from actions.js (written from UI-RING.md and ui_home_gesture()), and
// extract.py cross-checks that function's source cases. Hovering or activating a cell performs that
// input on the real firmware running on the stage watch, via synthesised pointer and button events.
import { h } from './dom.js';
import { INPUTS, STATE_ROWS, rowColor } from './actions.js';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const FX = { focus: 'Focus', ack: 'Ack', approve: 'Approve', deny: 'Deny', jump: 'Next ⚑', step: 'Next', none: '' };
// The mock scenario that puts one session in every state on the ring, in STATE_ROWS order.
const SCENARIO = 'ring';

export function createMatrix(root, ctx) {
  const f = ctx.facts, th = f.gesture, mid = f.display.width / 2;

  const caption = h('div', { class: 'mx__cap', 'aria-live': 'polite' },
    h('b', { class: 'mx__cap-t', text: 'Hover a cell' }), h('span', { class: 'mx__cap-d', text: 'to watch the real firmware do it.' }));
  const setCap = (title, desc, src) => {
    caption.replaceChildren(h('b', { class: 'mx__cap-t', text: title }), h('span', { class: 'mx__cap-d', text: desc }),
      src ? h('code', { class: 'mx__cap-s', text: src }) : null);
  };
  const src = (g) => f.gesture_map.cases.find((c) => c.gesture === g)?.source.replace('firmware/components/core/src/', '');
  const SRC = {
    tap: src('G_TAP'), long_press: src('G_LONG_PRESS'), hold: src('G_PRESS'), swipe_d: src('G_SWIPE_DOWN'),
    a: src('G_BTN_A'), a_double: src('G_BTN_A_DOUBLE'), b: src('G_BTN_B'),
  };

  const head = h('tr', null, h('th', { scope: 'col', class: 'mx__corner', text: '' }),
    INPUTS.map((i) => h('th', { scope: 'col', text: i.label })));
  const body = STATE_ROWS.map((r, ri) => h('tr', { style: { '--st': rowColor(r, f) } },
    h('th', { scope: 'row' }, h('i', { class: 'mx__sw' }), r.label),
    INPUTS.map((inp) => {
      const c = r.cells[inp.id];
      const btn = h('button', { type: 'button', class: 'mx__cell', 'data-fx': c.fx, 'aria-label': `${inp.label} on ${r.label}: ${c.text}` },
        h('span', { class: 'mx__fx', text: FX[c.fx] || '·' }));
      const show = () => setCap(`${inp.label} · ${r.label}`, c.text, SRC[inp.id]);
      let intent = 0;
      btn.addEventListener('pointerenter', (e) => { if (e.pointerType !== 'mouse') return; show(); clearTimeout(intent); intent = setTimeout(() => demo(ri, inp.id), 220); });
      btn.addEventListener('pointerleave', () => clearTimeout(intent));
      btn.addEventListener('focus', show);
      btn.addEventListener('click', () => { show(); demo(ri, inp.id); });
      return h('td', null, btn);
    })));

  const table = h('table', { class: 'mx__table' }, h('thead', null, head), h('tbody', null, body));
  const foot = h('p', { class: 'mx__foot' },
    `Approving is the only action that needs a hold (${th.hold_confirm_ms} ms), and only a permission request can be denied. ` +
    `A waits ${th.btn_double_ms} ms to see whether a second press follows; B never waits. A and B together do nothing yet. The first touch on a sleeping watch only wakes it.`);
  root.append(h('div', { class: 'mx__scroll', tabindex: 0, role: 'region', 'aria-label': 'Gesture to action matrix' }, table), caption, foot);

  // ── performing inputs on the watch ──
  let token = 0, ready = false, dirty = false;
  const W = ctx.watch;
  const ptr = (type, x, y) => { try { W?.pointer?.(type, x, y); } catch { /* watch optional */ } };
  const btn = async (id, ms = 90) => { W?.button?.(id, true); await sleep(ms); W?.button?.(id, false); };
  const trace = (pts) => { try { ctx.stage?.trace?.(pts); } catch { /* optional */ } };

  async function swipe(x0, y0, x1, y1, tk) {
    ptr('down', x0, y0);
    const n = 8, pts = [{ x: x0, y: y0, t: performance.now() }];
    for (let i = 1; i <= n; i++) {
      await sleep(18);
      if (tk !== token) { ptr('up', x1, y1); return; }
      const x = x0 + ((x1 - x0) * i) / n, y = y0 + ((y1 - y0) * i) / n;
      ptr('move', x, y); pts.push({ x, y, t: performance.now() }); trace(pts);
    }
    ptr('up', x1, y1);
  }
  async function tap(x, y, hold = 60) {
    ptr('down', x, y); trace([{ x, y, t: performance.now() }]); await sleep(hold); ptr('up', x, y);
  }
  // A tap on a ring segment selects it; segments are evenly spaced from 12 o'clock, clockwise.
  async function selectRow(i) {
    const a = (((i + 0.5) * 360) / STATE_ROWS.length - 90) * (Math.PI / 180), r = (th.rim_in_px + th.rim_out_px) / 2 - 8;
    await tap(mid + Math.cos(a) * r, mid + Math.sin(a) * r);
    await sleep(350);
  }

  async function perform(input, tk) {
    switch (input) {
      case 'tap': await tap(mid, mid); break;
      case 'long_press': ptr('down', mid, mid); await sleep(th.long_press_ms + 150); ptr('up', mid, mid); break;
      case 'hold': ptr('down', mid, mid); await sleep(th.hold_confirm_ms + 250); ptr('up', mid, mid); break;
      case 'swipe_d': await swipe(mid, mid - 80, mid, mid + 80, tk); break;
      case 'a': await btn('a'); break;
      case 'a_double': await btn('a'); await sleep(110); await btn('a'); break;
      case 'b': await btn('b'); break;
      default: break;
    }
  }

  async function demo(rowIdx, input) {
    if (!W) return;
    const tk = ++token;
    try {
      if (!ready || dirty || ctx.daemon?.scenario !== SCENARIO) {
        // approving or denying consumes the request, so the scenario starts again for a clean ring
        ctx.daemon?.play?.(SCENARIO);
        ready = true; dirty = false;
        await sleep(1700);
        if (tk !== token) return;
      }
      ctx.stage?.focus?.('screen');
      await selectRow(rowIdx);
      if (tk !== token) return;
      if ((input === 'hold' || input === 'swipe_d') && STATE_ROWS[rowIdx].id === 'perm') dirty = true;
      await perform(input, tk);
    } catch (e) { console.warn('[atlas] gesture demo failed', e); }
  }
  return { demo };
}
