// "What it shows": the parts of the ring screen (Sigil UI, ui_home_card.c), the six states and the
// link indicator. Anchors are canvas coordinates (466 px) read from the firmware through facts.ring,
// so a callout lands on the part it names. On entry the watch plays the `states` scenario: the ring
// with one session in every state, in the order of STATE_ROWS, and no live request.
/**
 * What this chapter uses of the page context.
 * @typedef {{ daemon?: { play?: (name: string) => void }, watch?: { screen?: () => string, pointer?: (type: string, x: number, y: number) => void }, stage?: { trace?: (pts: object[]) => void } }} GlanceCtx
 */
import { STATE_ROWS, stateLabel, rowColor } from '../../gestures/actions.js';

const mid = 233;
const RIM_R = 200; // inside the rim's touch band (ring_hit: r > 150)
const polar = (deg, r) => ({
  x: Math.round(mid + r * Math.sin((deg * Math.PI) / 180)),
  y: Math.round(mid - r * Math.cos((deg * Math.PI) / 180)),
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
/**
 * Centre angle of segment i of n (ui_ring.c ring_seg_span: clockwise from 12 o'clock).
 * @param {number} i the segment
 * @param {number} n how many segments
 * @returns {number} degrees
 */
const segDeg = (i, n) => ((i + 0.5) * 360) / n;

const parts = (f) => {
  const r = f.ring;
  return [
    {
      name: 'Segments',
      text: 'One arc per session, in the order they started. Drag round the rim, or tap an arc, to pick one; the picked arc is drawn wider.',
      at: { ...polar(segDeg(0, STATE_ROWS.length), 205), text: 'a session' },
    },
    {
      name: 'Sigil',
      text: 'The project, as a shape made from its name: the same repo always gets the same sigil. Under it, small marks: which agent, and whether it runs remote, snoozed or quiet.',
      at: { x: mid, y: r.sigil_y_px, text: 'project sigil' },
    },
    {
      name: 'State glyph',
      text: 'The state as one large glyph in the state colour. Tap it to open the session’s menu; when it needs you, the tap opens the request, and never answers it.',
      at: { x: mid, y: r.hero_y_px, text: 'state' },
    },
    {
      name: 'Time dots',
      text: 'How long the session has been in this state: one dot a minute round the glyph. A running job draws its progress as an arc inside them.',
      at: { x: mid + r.time_dots_r_px, y: r.hero_y_px, text: 'time in state' },
    },
    {
      name: 'Summary',
      text: 'The one place with words: what the session last said, or what it asks you.',
      at: { x: mid, y: r.summary_y_px, text: 'summary' },
    },
    {
      name: 'Key hints',
      text: 'Beside each key, on the upper rim. A (amber) brings this session’s terminal to the front; A twice jumps to the next session that needs you. B (blue) picks the next session. They are the only colours that are not states.',
      at: { x: r.key_a_x_px, y: r.key_y_px, text: 'key A' },
    },
    {
      name: 'Many sessions',
      text: `From ${r.merge_at} sessions, finished ones fold into one dim arc so the live ones keep room. Pick it and they spread out again.`,
      at: { ...polar(330, 205), text: 'folded Done' },
    },
  ];
};

const links = (f) => [
  {
    kind: null,
    name: 'Offline',
    color: 'var(--c-idle)',
    text: `No PC. ${f.link.silence_timeout_ms / 1000} s after the last message the watch shows the offline sign, with its age, under the sigil.`,
  },
  {
    kind: 'usb',
    name: 'USB',
    color: 'var(--usb)',
    text: 'Plugged into the PC. USB wins when both links are up, and the screen stays on for 10 minutes instead of 60 s: stand it on your desk.',
  },
  {
    kind: 'ble',
    name: 'BLE',
    color: 'var(--ble)',
    text: 'Bluetooth: the same messages, cut into pieces that fit. The first time, you type the six-digit passkey the watch shows; after that only watches you trust are served.',
  },
];

const h = (tag, cls, html) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (html != null) n.innerHTML = html;
  return n;
};
const esc = (s) =>
  String(s).replace(
    /[&<>"]/g,
    (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c],
  );

let token = 0;

/**
 * Plays the `states` scenario and waits until the watch shows the ring.
 * @param {GlanceCtx} ctx the page context
 * @returns {Promise<boolean>} false when a later call took over
 */
async function showRing(ctx) {
  const tk = ++token;
  ctx.daemon?.play?.('states');
  for (let i = 0; i < 20 && tk === token; i++) {
    await sleep(100);
    if (ctx.watch?.screen?.() === 'ring' || !ctx.watch?.screen) break;
  }
  return tk === token;
}

/**
 * Taps the rim over segment i on the real watch, which selects that session (nav.c: rim tap).
 * @param {GlanceCtx} ctx the page context
 * @param {number} i the segment, in STATE_ROWS order
 * @returns {Promise<void>}
 */
async function pick(ctx, i) {
  const W = ctx.watch;
  if (!W?.pointer) return;
  if (W.screen?.() !== 'ring' && !(await showRing(ctx))) return;
  const p = polar(segDeg(i, STATE_ROWS.length), RIM_R);
  try {
    ctx.stage?.trace?.([{ ...p, t: performance.now() }]);
    W.pointer('down', p.x, p.y);
    await sleep(60);
    W.pointer('up', p.x, p.y);
  } catch {
    /* the watch is optional */
  }
}

export function mount(section, ctx) {
  const f = ctx.facts;
  const chips = section.querySelector('[data-slot="chips"]');
  const screens = section.querySelector('[data-slot="screens"]');
  const link = section.querySelector('[data-slot="link"]');

  STATE_ROWS.forEach((row, i) => {
    const color = rowColor(row, f),
      label = stateLabel(row, f),
      shape = f.ring.kinds?.[row.kind];
    const b = h('button', 'state-chip');
    b.type = 'button';
    b.style.setProperty('--c', color);
    b.setAttribute('aria-pressed', 'false');
    b.innerHTML = `<i class="state-chip__dot" aria-hidden="true"></i><span class="state-chip__name">${esc(label)}</span>${shape ? `<small class="state-chip__src">${esc(shape)}</small>` : ''}`;
    b.addEventListener('click', () => {
      chips
        .querySelectorAll('.state-chip')
        .forEach((c) => c.setAttribute('aria-pressed', String(c === b)));
      ctx.stage.setGlow(color, 0.26, 1800);
      ctx.bus.emit('glance:state', { id: row.id, color, label, shape });
      pick(ctx, i);
    });
    chips.appendChild(b);
  });

  parts(f).forEach((p, i) => {
    const c = h('article', 'screen-card');
    c.tabIndex = 0;
    c.innerHTML = `<span class="screen-card__n mono">0${i + 1}</span><h3>${p.name}</h3><p>${p.text}</p>`;
    const on = () => ctx.stage.annotate('part', p.at);
    const off = () => ctx.stage.annotate('part', null);
    c.addEventListener('pointerenter', on);
    c.addEventListener('focus', on);
    c.addEventListener('pointerleave', off);
    c.addEventListener('blur', off);
    screens.appendChild(c);
  });

  const linkRows = links(f);
  const group = h('div', 'segmented');
  group.setAttribute('role', 'radiogroup');
  group.setAttribute('aria-label', 'Link indicator');
  const note = h('p', 'segmented__note');
  linkRows.forEach((l, i) => {
    const b = h(
      'button',
      'segmented__opt',
      `<i style="background:${l.color}" aria-hidden="true"></i>${l.name}`,
    );
    b.type = 'button';
    b.setAttribute('role', 'radio');
    b.setAttribute('aria-checked', String(i === 0));
    b.addEventListener('click', () => {
      group
        .querySelectorAll('button')
        .forEach((x) => x.setAttribute('aria-checked', String(x === b)));
      note.textContent = l.text;
      for (const k of ['usb', 'ble']) ctx.watch.setLinkUp?.(k, k === l.kind);
      if (l.kind) ctx.stage.pulse(l.kind);
    });
    group.appendChild(b);
  });
  note.textContent = linkRows[0].text;
  link.append(h('h3', 'chapter__sub', 'Link'), group, note);
}

/**
 * Puts the watch on the ring with one session in every state.
 * @param {object} ctx the page context
 */
export function onEnter(ctx) {
  showRing(ctx);
}

export function onLeave(ctx) {
  token++;
  ctx.stage.annotate('state', null);
  ctx.stage.annotate('part', null);
}
