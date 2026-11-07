// Gesture → action matrix, built from facts.gesture_map (parsed from ui_*.c). Hovering or
// activating a cell performs that gesture on the real watch via synthesised pointer events.
import { h } from './dom.js';

const COLS = [
  ['G_TAP', 'tap', 'Tap'], ['G_DOUBLE_TAP', 'double_tap', 'Double tap'], ['G_LONG_PRESS', 'long_press', 'Long press'],
  ['G_SWIPE_UP', 'swipe_u', 'Swipe ↑'], ['G_SWIPE_DOWN', 'swipe_d', 'Swipe ↓'],
  ['G_SWIPE_LEFT', 'swipe_l', 'Swipe ←'], ['G_SWIPE_RIGHT', 'swipe_r', 'Swipe →'],
  ['G_BTN_A', 'btn_a', 'Button A'], ['G_BTN_B', 'btn_b', 'Button B'], ['G_BTN_B_LONG', 'btn_b_long', 'Hold B'],
];
const ROWS = [['glance', 'Glance'], ['list', 'List'], ['detail', 'Detail']];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function createMatrix(root, ctx, lab) {
  const f = ctx.facts, th = f.gesture, cx = f.display.width / 2;
  const map = f.gesture_map;
  const cell = (screen, g) => {
    const hit = (map[screen] || []).find((c) => c.gesture === g);
    if (hit) return hit;
    return (map.global || []).find((c) => c.gesture === g) || null;
  };
  const defers = (screen, g) => g === 'G_TAP' && (screen === 'detail' || screen === 'list');

  const caption = h('div', { class: 'mx__cap', 'aria-live': 'polite' },
    h('b', { class: 'mx__cap-t', text: 'Hover a cell' }), h('span', { class: 'mx__cap-d', text: 'to watch the real firmware do it.' }));
  const setCap = (title, desc, src) => {
    caption.replaceChildren(h('b', { class: 'mx__cap-t', text: title }), h('span', { class: 'mx__cap-d', text: desc }),
      src ? h('code', { class: 'mx__cap-s', text: src.replace('firmware/components/core/src/', '') }) : null);
  };

  const head = h('tr', null, h('th', { scope: 'col', class: 'mx__corner', text: '' }),
    COLS.map(([, , label]) => h('th', { scope: 'col', text: label })));
  const body = ROWS.map(([id, label]) => h('tr', null,
    h('th', { scope: 'row', text: label }),
    COLS.map(([g, kind, glabel]) => {
      const c = cell(id, g);
      if (!c) return h('td', { class: 'mx__empty' }, h('span', { 'aria-label': `${glabel} on ${label}: nothing`, text: '·' }));
      const btn = h('button', {
        type: 'button', class: 'mx__cell', 'data-defer': defers(id, g) ? '1' : '', 'data-global': map.global?.includes(c) ? '1' : '',
        'aria-label': `${glabel} on ${label}: ${c.action}`,
      }, h('i', { class: 'mx__dot' }), defers(id, g) ? h('em', { class: 'mx__defer', text: `${th.tap_defer_ms}` }) : null);
      const show = () => setCap(`${glabel} · ${label}`, c.action + (defers(id, g) ? ` (the tap waits ${th.tap_defer_ms} ms)` : ''), c.source);
      let intent = 0;
      btn.addEventListener('pointerenter', (e) => { if (e.pointerType !== 'mouse') return; show(); clearTimeout(intent); intent = setTimeout(() => demo(id, kind), 220); });
      btn.addEventListener('pointerleave', () => clearTimeout(intent));
      btn.addEventListener('focus', show);
      btn.addEventListener('click', () => { show(); demo(id, kind); });
      return h('td', null, btn);
    })));

  const table = h('table', { class: 'mx__table' }, h('thead', null, head), h('tbody', null, body));
  const foot = h('p', { class: 'mx__foot', text: `Detail also watches raw press and release: holding Approve for ${th.hold_confirm_ms} ms confirms, lifting early cancels. Shake jumps to Glance from anywhere.` });
  root.append(h('div', { class: 'mx__scroll', tabindex: 0, role: 'region', 'aria-label': 'Gesture to action matrix' }, table), caption, foot);

  // ── performing gestures on the watch ──
  let token = 0, started = false;
  const W = ctx.watch;
  const ptr = (type, x, y) => { try { W?.pointer?.(type, x, y); } catch { /* watch optional */ } lab?.feed(type, x, y, 'demo'); };
  const btn = async (id, ms = 90) => { W?.button?.(id, true); await sleep(ms); W?.button?.(id, false); };

  async function swipe(x0, y0, x1, y1, tk) {
    ptr('down', x0, y0);
    const n = 8;
    for (let i = 1; i <= n; i++) { await sleep(18); if (tk !== token) { ptr('up', x1, y1); return; } ptr('move', x0 + ((x1 - x0) * i) / n, y0 + ((y1 - y0) * i) / n); }
    ptr('up', x1, y1);
  }
  async function tap(x, y, hold = 60) { ptr('down', x, y); await sleep(hold); ptr('up', x, y); }

  async function perform(kind, tk) {
    const y = cx;
    switch (kind) {
      case 'tap': await tap(cx, y); break;
      case 'double_tap': await tap(cx, y); await sleep(110); await tap(cx, y); break;
      case 'long_press': ptr('down', cx, y); await sleep(th.long_press_ms + 120); ptr('up', cx, y); break;
      case 'swipe_u': await swipe(cx, y + 90, cx, y - 90, tk); break;
      case 'swipe_d': await swipe(cx, y - 90, cx, y + 90, tk); break;
      case 'swipe_l': await swipe(cx + 100, y, cx - 100, y, tk); break;
      case 'swipe_r': await swipe(cx - 100, y, cx + 100, y, tk); break;
      case 'btn_a': await btn('a'); break;
      case 'btn_b': await btn('b'); break;
      case 'btn_b_long': await btn('b', th.long_press_ms + 150); break;
    }
  }

  async function demo(screen, kind) {
    if (!W) return;
    const tk = ++token;
    try {
      lab?.setScreen(screen);
      if (!started && ctx.daemon?.play) {
        started = true;
        const first = ctx.daemon.scenarios?.[0]?.name;
        try { ctx.daemon.play(first); } catch { /* optional */ }
        await sleep(1500);
        if (tk !== token) return;
      }
      ctx.stage?.focus?.('screen');
      // reach the screen: Hold B always lands on Glance, A opens List, A again opens Detail
      await btn('b', th.long_press_ms + 150); await sleep(500);
      if (tk !== token) return;
      if (screen !== 'glance') { await btn('a'); await sleep(550); }
      if (tk !== token) return;
      if (screen === 'detail') { await btn('a'); await sleep(550); }
      if (tk !== token) return;
      await perform(kind, tk);
    } catch (e) { console.warn('[atlas] gesture demo failed', e); }
  }
  return { demo };
}
