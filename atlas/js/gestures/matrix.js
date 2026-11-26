// What each gesture and key does, screen by screen. Every row comes from facts.gesture_map.cases, which
// atlas/tools/extract.py reads out of firmware/components/core/src/nav.c (the navigation table the watch
// runs), so the table cannot describe a UI the watch does not have. On the Ring tab a row can also be
// performed on the real firmware running on the stage watch, via synthesised pointer and button events.
import { h } from './dom.js';

/**
 * One row of nav.c as extract.py reads it (facts.gesture_map.cases).
 * @typedef {{ mode: string, gesture: string, zone: string, when: string, nav: string, action: string, source: string }} NavCase
 */

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// The screens a reader meets, in the order they meet them; nav.c's mode names on the right.
const SCREENS = [
  { mode: 'home', label: 'Ring' },
  { mode: 'agent', label: 'Agent menu' },
  { mode: 'ask', label: 'Request' },
  { mode: 'read', label: 'Last message' },
  { mode: 'status', label: 'Status' },
  { mode: 'scroll', label: 'Scroll' },
];

// Reader names for the firmware's gesture kinds. Kinds missing here are internal steps of another
// gesture (the start and end of a drag, a press before it becomes a hold) and are not rows of their own.
/** @type {Record<string, string>} */
const GESTURES = {
  G_TAP: 'Tap',
  G_DOUBLE_TAP: 'Double tap',
  G_LONG_PRESS: 'Long press',
  G_PRESS: 'Press and hold',
  G_SCRUB: 'Drag round the rim',
  G_DRAG: 'Drag',
  G_SWIPE_UP: 'Swipe up',
  G_SWIPE_DOWN: 'Swipe down',
  G_SWIPE_RIGHT: 'Swipe right',
  G_BTN_A: 'Key A',
  G_BTN_A_DOUBLE: 'Key A twice',
  G_BTN_A_LONG: 'Hold key A',
  G_BTN_B: 'Key B',
  G_BTN_B_LONG: 'Hold key B',
  G_CHORD: 'Keys A + B held',
};
const ORDER = Object.keys(GESTURES);

/** @type {Record<string, string>} */
const ZONE = { centre: 'centre', rim: 'rim', any: '' };

/**
 * Rows for one screen: one per gesture, each with every case (zone and condition) the firmware binds.
 * @param {NavCase[]} cases facts.gesture_map.cases
 * @param {string} mode a nav.c mode name
 * @returns {{ gesture: string, label: string, cases: NavCase[] }[]} the rows, in reading order
 */
export function screenRows(cases, mode) {
  const rows = new Map();
  for (const c of cases) {
    if (c.mode !== mode || !GESTURES[c.gesture]) continue;
    if (!rows.has(c.gesture)) rows.set(c.gesture, []);
    rows.get(c.gesture).push(c);
  }
  return ORDER.filter((g) => rows.has(g)).map((g) => ({
    gesture: g,
    label: GESTURES[g],
    cases: rows.get(g),
  }));
}

const where = (c) =>
  [ZONE[c.zone] ?? c.zone, c.when && c.when !== 'always' ? c.when : ''].filter(Boolean).join(', ');
const short = (src) => src?.replace('firmware/components/core/src/', '');

export function createMatrix(root, ctx) {
  const f = ctx.facts,
    th = f.gesture,
    mid = f.display.width / 2,
    cases = f.gesture_map?.cases ?? [];

  const caption = h(
    'div',
    { class: 'mx__cap', 'aria-live': 'polite' },
    h('b', { class: 'mx__cap-t', text: 'Pick a gesture on the Ring tab' }),
    h('span', { class: 'mx__cap-d', text: 'to watch the real firmware do it.' }),
  );
  const setCap = (title, desc, src) =>
    caption.replaceChildren(
      h('b', { class: 'mx__cap-t', text: title }),
      h('span', { class: 'mx__cap-d', text: desc }),
      src ? h('code', { class: 'mx__cap-s', text: src }) : null,
    );

  const tabs = h('div', { class: 'mx__tabs', role: 'tablist', 'aria-label': 'Screens' });
  const panel = h('div', { class: 'mx__scroll', tabindex: 0, role: 'tabpanel' });
  let current = 'home';

  function render(mode) {
    current = mode;
    for (const b of tabs.children) b.setAttribute('aria-selected', String(b.dataset.mode === mode));
    const live = mode === 'home';
    const rows = screenRows(cases, mode).map((r) => {
      const lines = r.cases.map((c) =>
        h(
          'li',
          null,
          where(c) ? h('span', { class: 'mx__when', text: `${where(c)}: ` }) : null,
          c.action,
        ),
      );
      const name = live
        ? h('button', { type: 'button', class: 'mx__cell', 'data-g': r.gesture, text: r.label })
        : h('span', { class: 'mx__name', text: r.label });
      if (live) {
        const show = () =>
          setCap(r.label, r.cases.map((c) => c.action).join(' / '), short(r.cases[0].source));
        name.addEventListener('focus', show);
        name.addEventListener('click', () => {
          show();
          demo(r.gesture);
        });
      }
      return h(
        'tr',
        null,
        h('th', { scope: 'row' }, name),
        h('td', null, h('ul', { class: 'mx__cases' }, lines)),
        h('td', { class: 'mx__src' }, h('code', { text: short(r.cases[0].source) })),
      );
    });
    panel.replaceChildren(
      h(
        'table',
        { class: 'mx__table mx__table--screen' },
        h(
          'thead',
          null,
          h(
            'tr',
            null,
            h('th', { scope: 'col', text: 'Gesture' }),
            h('th', { scope: 'col', text: 'What it does' }),
            h('th', { scope: 'col', text: 'Source' }),
          ),
        ),
        h('tbody', null, rows),
      ),
    );
    caption.hidden = !live;
  }

  for (const s of SCREENS) {
    if (!cases.some((c) => c.mode === s.mode)) continue;
    const b = h('button', {
      type: 'button',
      role: 'tab',
      class: 'mx__tab',
      'data-mode': s.mode,
      text: s.label,
    });
    b.addEventListener('click', () => render(s.mode));
    tabs.append(b);
  }

  const chord = f.gesture_map?.chord;
  const foot = h(
    'p',
    { class: 'mx__foot' },
    `Key A waits ${th.btn_double_ms} ms to see whether a second press follows. ` +
      (chord ? `${chord}. ` : '') +
      (f.gesture_map?.first_touch_wakes_only
        ? 'The first touch on a sleeping watch only wakes it. '
        : '') +
      'Every row is read from the navigation table the watch runs (nav.c); the Source column is its line.',
  );
  root.append(tabs, panel, caption, foot);
  render(current);

  // ── performing a Ring gesture on the watch ──
  let token = 0;
  const W = ctx.watch;
  const ptr = (type, x, y) => {
    try {
      W?.pointer?.(type, x, y);
    } catch {
      /* watch optional */
    }
  };
  const key = async (id, ms = 90) => {
    W?.button?.(id, true);
    await sleep(ms);
    W?.button?.(id, false);
  };
  const trace = (pts) => {
    try {
      ctx.stage?.trace?.(pts);
    } catch {
      /* optional */
    }
  };
  async function swipe(x0, y0, x1, y1, tk) {
    ptr('down', x0, y0);
    const n = 8,
      pts = [{ x: x0, y: y0, t: performance.now() }];
    for (let i = 1; i <= n; i++) {
      await sleep(18);
      if (tk !== token) {
        ptr('up', x1, y1);
        return;
      }
      const x = x0 + ((x1 - x0) * i) / n,
        y = y0 + ((y1 - y0) * i) / n;
      ptr('move', x, y);
      pts.push({ x, y, t: performance.now() });
      trace(pts);
    }
    ptr('up', x1, y1);
  }
  async function tap(x, y, hold = 60) {
    ptr('down', x, y);
    trace([{ x, y, t: performance.now() }]);
    await sleep(hold);
    ptr('up', x, y);
  }
  async function scrub(tk) {
    const r = (th.rim_in_px + th.rim_out_px) / 2 - 8;
    const at = (deg) => [
      mid + Math.sin((deg * Math.PI) / 180) * r,
      mid - Math.cos((deg * Math.PI) / 180) * r,
    ];
    let [x, y] = at(0);
    ptr('down', x, y);
    const pts = [{ x, y, t: performance.now() }];
    for (let d = 6; d <= 90; d += 6) {
      await sleep(25);
      if (tk !== token) break;
      [x, y] = at(d);
      ptr('move', x, y);
      pts.push({ x, y, t: performance.now() });
      trace(pts);
    }
    ptr('up', x, y);
  }

  /** @type {Record<string, (tk: number) => Promise<void>>} */
  const PERFORM = {
    G_TAP: () => tap(mid, mid),
    G_DOUBLE_TAP: async () => {
      await tap(mid, mid);
      await sleep(120);
      await tap(mid, mid);
    },
    G_SCRUB: (tk) => scrub(tk),
    G_SWIPE_UP: (tk) => swipe(mid, mid + 80, mid, mid - 80, tk),
    G_SWIPE_DOWN: (tk) => swipe(mid, mid - 80, mid, mid + 80, tk),
    G_BTN_A: () => key('a'),
    G_BTN_A_DOUBLE: async () => {
      await key('a');
      await sleep(110);
      await key('a');
    },
    G_BTN_B: () => key('b'),
    G_BTN_B_LONG: () => key('b', th.long_press_ms + 150),
    G_CHORD: async () => {
      W?.button?.('a', true);
      W?.button?.('b', true);
      await sleep(th.long_press_ms + 150);
      W?.button?.('a', false);
      W?.button?.('b', false);
    },
  };

  async function demo(gesture) {
    const run = PERFORM[gesture];
    if (!W || !run) return;
    const tk = ++token;
    try {
      // back to the ring with one session in every state, so each gesture starts from the same screen
      ctx.daemon?.play?.('states');
      await sleep(1700);
      if (tk !== token) return;
      ctx.stage?.focus?.('screen');
      await run(tk);
    } catch (e) {
      console.warn('[atlas] gesture demo failed', e);
    }
  }
  return { demo };
}
