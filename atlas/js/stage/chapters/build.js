// "Under the hood": exploded device, the parts on the rim, the software map, and how the page stays
// true. Hardware from docs/StopWatch.md (M5Stack C152 spec) and firmware/boards/stopwatch/.

/**
 * @typedef {object} BuildCtx
 * @property {{focus: (region: string | null) => void}} stage the watch stage (spotlight)
 * @property {{messages?: unknown[], display?: {width: number, height: number}, generated_from?: string}} facts facts.json
 * @property {{kind: string}} watch the watch runtime ('wasm' or 'stub')
 * @property {boolean} [factsGenerated] whether facts.json loaded
 * @property {boolean} reducedMotion prefers-reduced-motion
 */

/**
 * A part. `z` is its level in the exploded stack (0 = bottom); parts on one level sit side by side.
 * `focus` is the stage region (art.js REGIONS) to spotlight on the watch, or null.
 * @typedef {{id: string, name: string, note: string, z: number, focus: string | null}} Part
 */

/** @type {Part[]} the stack, top to bottom */
const LAYERS = [
  { id: 'glass', name: 'Cover glass', note: 'over the touch panel', z: 5, focus: 'screen' },
  { id: 'amoled', name: 'AMOLED · CO5300', note: '1.75″, QSPI', z: 4, focus: 'screen' },
  { id: 'touch', name: 'Touch · CST820B', note: 'capacitive, I²C', z: 3, focus: 'screen' },
  {
    id: 'esp',
    name: 'ESP32-S3R8',
    note: '16 MB flash · 8 MB PSRAM · Wi-Fi, BLE',
    z: 2,
    focus: 'antenna',
  },
  { id: 'pm1', name: 'Power · M5PM1', note: 'rails, charging, power key', z: 2, focus: 'power' },
  {
    id: 'ioe',
    name: 'IO expander · M5IOE1',
    note: 'motor PWM, amp enable, resets',
    z: 2,
    focus: null,
  },
  { id: 'imu', name: 'IMU · BMI270', note: '6-axis: raise to wake, face down', z: 2, focus: null },
  { id: 'rtc', name: 'Clock · RX8130CE', note: 'real-time clock', z: 2, focus: null },
  { id: 'audio', name: 'Audio · ES8311', note: 'codec + MEMS mic', z: 2, focus: null },
  { id: 'speaker', name: 'Speaker', note: 'AW8737A amp · 1 W, 8 Ω', z: 1, focus: null },
  { id: 'motor', name: 'Vibration motor', note: 'the haptic voice', z: 1, focus: 'motor' },
  { id: 'battery', name: 'Battery', note: '450 mAh', z: 0, focus: null },
];

/**
 * Parts on the rim: `dot` is the glyph colour (the key caps' colours), or null for a port.
 * @type {{id: string, name: string, note: string, dot: string | null, focus: string | null}[]}
 */
const RIM = [
  { id: 'keya', name: 'Key A', note: 'upper left', dot: '#f5a417', focus: 'keya' },
  { id: 'keyb', name: 'Key B', note: 'upper right', dot: '#1f6fd6', focus: 'keyb' },
  { id: 'power', name: 'Power', note: 'lower left · M5PM1', dot: '#e3342f', focus: 'power' },
  { id: 'usb', name: 'USB-C', note: '5 V in, data', dot: null, focus: 'usb' },
  { id: 'grove', name: 'HY2.0-4P', note: 'Grove port', dot: null, focus: 'grove' },
  { id: 'rear', name: 'Rear bus', note: '2.54 mm 7P + 6P', dot: null, focus: null },
];

/** The codebases, one per top-level directory in the README's repository map. */
const MAP = [
  {
    title: 'Firmware',
    blurb: 'C. A portable core (UI, gestures, feedback, link, model) over a HAL and the board.',
    color: 'var(--c-accent)',
    links: [
      ['firmware/components/core/src/', 'core'],
      ['firmware/boards/stopwatch/', 'board'],
      ['docs/design/FIRMWARE.md', 'doc'],
    ],
  },
  {
    title: 'Simulator',
    blurb: 'C. The same core on SDL2, linked to the daemon over WebSocket.',
    color: 'var(--c-amber)',
    links: [['simulator/', 'source']],
  },
  {
    title: 'Daemon',
    blurb: 'Rust. agentlinkd watches agents, owns the transports, focuses terminals.',
    color: 'var(--c-success)',
    links: [
      ['daemon/crates/agentlinkd/src/', 'source'],
      ['daemon/crates/agentlink-hook/src/', 'hook'],
      ['docs/design/DESIGN.md', 'doc'],
    ],
  },
  {
    title: 'Protocol',
    blurb: 'CDDL. CBOR envelopes, COBS + CRC over USB, fragments over BLE.',
    color: 'var(--c-cloud)',
    links: [
      ['protocol/agentlink.cddl', 'schema'],
      ['protocol/vectors/', 'vectors'],
      ['docs/design/PROTOCOL.md', 'doc'],
    ],
  },
  {
    title: 'Atlas',
    blurb: 'JS. This page: the core in WebAssembly and a mock daemon.',
    color: 'var(--paper)',
    links: [
      ['atlas/', 'source'],
      ['atlas/CONTRACT.md', 'doc'],
    ],
  },
];

const WORDS = [
  'Zero',
  'One',
  'Two',
  'Three',
  'Four',
  'Five',
  'Six',
  'Seven',
  'Eight',
  'Nine',
  'Ten',
];
WORDS.push(
  'Eleven',
  'Twelve',
  'Thirteen',
  'Fourteen',
  'Fifteen',
  'Sixteen',
  'Seventeen',
  'Eighteen',
);
WORDS.push('Nineteen', 'Twenty');

/**
 * @param {string} tag element name
 * @param {string} [cls] class names
 * @param {string} [html] inner markup
 * @returns {HTMLElement} the new element
 */
const h = (tag, cls, html) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (html != null) n.innerHTML = html;
  return n;
};

/**
 * @param {number} n a count
 * @returns {string} n as a word when it is small, else digits
 */
const word = (n) => WORDS[n] ?? String(n);

/**
 * One part in the exploded stack.
 * @param {Part} l the part
 * @param {number} n its number in the list
 * @returns {string} markup
 */
function layerHTML(l, n) {
  const inner = {
    amoled: '<b class="ex__ring"></b>',
    touch: '<b class="ex__grid"></b>',
    esp: '<b class="ex__pcb"></b><b class="ex__chip ex__chip--big"></b><b class="ex__chip ex__chip--ant"></b>',
    motor: '<b class="ex__rotor"></b>',
    speaker: '<b class="ex__cone"></b>',
  }[l.id];
  // parts on the board level sit a hair above it so they do not z-fight
  const z = l.z === 2 && l.id !== 'esp' ? 2.03 : l.z;
  return `<div class="ex ex--${l.id}" data-layer="${l.id}" style="--i:${z}">${inner ?? ''}<span class="ex__n mono">${n}</span></div>`;
}

/**
 * Light one part in the stack and the lists, or none.
 * @param {HTMLElement} wrap the .explode element
 * @param {string | null} id the part, or null for none
 */
function hot(wrap, id) {
  if (id) wrap.dataset.hot = id;
  else delete wrap.dataset.hot;
  wrap.querySelectorAll('[data-layer]').forEach((el) => {
    el.classList.toggle('is-hot', !!id && /** @type {HTMLElement} */ (el).dataset.layer === id);
  });
}

/** @type {HTMLElement | null} */
let wrapEl = null;

/**
 * @param {HTMLElement} section the #build section
 * @param {BuildCtx} ctx the shared context
 */
export function mount(section, ctx) {
  const scene = /** @type {HTMLElement} */ (section.querySelector('.explode__scene'));
  const list = /** @type {HTMLElement} */ (section.querySelector('.explode__list'));
  const rim = /** @type {HTMLElement} */ (section.querySelector('.explode__rim'));
  const wrap = /** @type {HTMLElement} */ (section.querySelector('.explode'));
  wrapEl = wrap;

  // draw bottom → top so a higher part paints over a lower one
  scene.innerHTML = `<div class="explode__stack">${LAYERS.map((l, i) => layerHTML(l, i + 1))
    .reverse()
    .join('')}</div>`;

  const { width = 466, height = 466 } = ctx.facts.display ?? {};
  /**
   * Hover or focus spotlights the part on the watch and in the stack.
   * @param {HTMLElement} el the list row
   * @param {string} id the part
   * @param {string | null} focus the stage region, or null
   */
  const wire = (el, id, focus) => {
    el.tabIndex = 0;
    el.dataset.layer = id;
    const on = () => {
      hot(wrap, id);
      ctx.stage.focus(focus);
    };
    const off = () => {
      hot(wrap, null);
      ctx.stage.focus(null);
    };
    el.addEventListener('pointerenter', on);
    el.addEventListener('focus', on);
    el.addEventListener('pointerleave', off);
    el.addEventListener('blur', off);
  };

  LAYERS.forEach((l, i) => {
    const note = l.id === 'amoled' ? `${l.note} · ${width}×${height}` : l.note;
    const li = h('li', 'explode__item');
    li.innerHTML = `<b class="mono">${i + 1}</b><div><strong>${l.name}</strong><span>${note}</span></div>`;
    wire(li, l.id, l.focus);
    list.appendChild(li);
  });
  RIM.forEach((r) => {
    const li = h('li', 'rimpart');
    li.dataset.part = r.id;
    const glyph = r.dot
      ? `<i class="rimpart__dot" style="--k:${r.dot}"></i>`
      : `<i class="rimpart__port"></i>`;
    li.innerHTML = `${glyph}<div><strong>${r.name}</strong><span>${r.note}</span></div>`;
    wire(li, r.id, r.focus);
    rim.appendChild(li);
  });
  scene.querySelectorAll('.ex').forEach((node) => {
    const el = /** @type {HTMLElement} */ (node);
    el.addEventListener('pointerenter', () => hot(wrap, el.dataset.layer ?? null));
    el.addEventListener('pointerleave', () => hot(wrap, null));
  });

  // the eyebrow counts the parts and codebases listed here, so it cannot drift from them
  const count = section.querySelector('[data-slot="count"]');
  if (count)
    count.textContent = `${word(LAYERS.length + RIM.length)} parts, ${word(MAP.length).toLowerCase()} codebases`;

  const grid = /** @type {HTMLElement} */ (section.querySelector('.softmap__grid'));
  for (const m of MAP) {
    const c = h('article', 'softmap__card');
    c.style.setProperty('--c', m.color);
    c.innerHTML = `<h4>${m.title}</h4><p>${m.blurb}</p><ul>${m.links.map(([href, label]) => `<li><a href="${href}"><span class="mono">${href}</span><em>${label}</em></a></li>`).join('')}</ul>`;
    grid.appendChild(c);
  }
  const msgs = ctx.facts.messages?.length;
  const proto = grid.children[3]?.querySelector('p');
  if (msgs && proto) proto.textContent += ` ${msgs} message types.`;

  const rt = /** @type {HTMLElement} */ (section.querySelector('[data-badge="runtime"]'));
  const fk = /** @type {HTMLElement} */ (section.querySelector('[data-badge="facts"]'));
  const wasm = ctx.watch.kind === 'wasm';
  rt.className = `badge ${wasm ? 'badge--live' : 'badge--off'}`;
  rt.textContent = wasm ? 'Watch: real firmware (WASM)' : 'Watch: stub preview';
  fk.className = `badge ${ctx.factsGenerated ? 'badge--live' : 'badge--off'}`;
  fk.textContent = ctx.factsGenerated
    ? `Facts: generated from ${String(ctx.facts.generated_from ?? 'source').slice(0, 10)}`
    : 'Facts not generated';

  if (!ctx.factsGenerated) {
    const f = h('div', 'badge badge--float', 'facts not generated');
    f.title =
      'atlas/data/facts.json is missing. Showing built-in fallbacks. Run atlas/tools/extract.py.';
    document.body.appendChild(f);
  }
}

/** @param {BuildCtx} ctx the shared context */
export function onEnter(ctx) {
  const wrap = wrapEl;
  if (!wrap) return;
  if (ctx.reducedMotion) wrap.classList.add('is-open');
  else setTimeout(() => wrap.classList.add('is-open'), 220);
}

/** @param {BuildCtx} ctx the shared context */
export function onLeave(ctx) {
  if (wrapEl && !ctx.reducedMotion) wrapEl.classList.remove('is-open');
  ctx.stage.focus(null);
}
