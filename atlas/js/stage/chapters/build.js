// "Under the hood": exploded device, software map, and how the page stays true.
const LAYERS = [
  { id: 'glass',   name: 'Cover glass',        note: 'Ø 44 mm viewable, bonded', focus: 'screen' },
  { id: 'amoled',  name: 'AMOLED · CO5300',    note: '1.75″, 466×466, true black', focus: 'screen' },
  { id: 'touch',   name: 'Touch · CST820',     note: 'capacitive, I²C', focus: 'screen' },
  { id: 'esp',     name: 'ESP32-S3R8 module',  note: '16 MB flash · 8 MB PSRAM · BLE', focus: 'antenna' },
  { id: 'pm1',     name: 'PM1 power chip',     note: 'M5PM1 · rails, charging', focus: 'usb' },
  { id: 'ioe',     name: 'IO expander',        note: 'M5IOE1 · keys, haptic enable', focus: 'buttons' },
  { id: 'motor',   name: 'Vibration motor',    note: 'the haptic voice', focus: 'motor' },
  { id: 'battery', name: 'Battery',            note: '450 mAh', focus: null },
];

const MAP = [
  { title: 'Firmware core', blurb: 'UI, gestures, feedback, link, model. Pure C, host-testable.', color: 'var(--c-accent)',
    links: [['firmware/components/core/src/', 'source'], ['docs/design/FIRMWARE.md', 'doc']] },
  { title: 'Board HAL', blurb: 'Display, touch, PMIC, motor, USB-CDC and BLE for the real board.', color: 'var(--c-amber)',
    links: [['firmware/components/agentlink_hal/', 'source'], ['firmware/boards/', 'boards']] },
  { title: 'Daemon crates', blurb: 'agentlinkd watches agents, owns the transports, focuses terminals.', color: 'var(--c-success)',
    links: [['daemon/crates/agentlinkd/src/', 'source'], ['daemon/crates/agentlink-hook/src/', 'hook'], ['docs/design/DESIGN.md', 'doc']] },
  { title: 'Protocol', blurb: 'CBOR envelopes, COBS + CRC over USB, fragments over BLE.', color: 'var(--c-cloud)',
    links: [['protocol/agentlink.cddl', 'schema'], ['protocol/vectors/', 'vectors'], ['docs/design/PROTOCOL.md', 'doc']] },
];

const h = (tag, cls, html) => { const n = document.createElement(tag); if (cls) n.className = cls; if (html != null) n.innerHTML = html; return n; };

function layerHTML(l, i) {
  const inner = {
    glass: '', amoled: '<b class="ex__ring"></b>', touch: '<b class="ex__grid"></b>',
    esp: '<b class="ex__pcb"></b><b class="ex__chip ex__chip--big"></b><b class="ex__chip ex__chip--ant"></b>',
    pm1: '', ioe: '', motor: '<b class="ex__rotor"></b>', battery: '',
  }[l.id];
  return `<div class="ex ex--${l.id}" data-layer="${l.id}" style="--i:${i}">${inner}<span class="ex__n mono">${i + 1}</span></div>`;
}

export function mount(section, ctx) {
  const scene = section.querySelector('.explode__scene');
  const list = section.querySelector('.explode__list');
  const wrap = section.querySelector('.explode');

  // stack bottom → top: the last item is the cover glass
  scene.innerHTML = `<div class="explode__stack">${[...LAYERS].reverse().map((l, k) => layerHTML(l, k)).join('')}</div>`;
  const num = (id) => LAYERS.findIndex((l) => l.id === id) + 1;
  scene.querySelectorAll('.ex__n').forEach((n) => { n.textContent = num(n.parentElement.dataset.layer); });

  LAYERS.forEach((l, i) => {
    const li = h('li', 'explode__item');
    li.tabIndex = 0;
    li.dataset.layer = l.id;
    li.innerHTML = `<b class="mono">${i + 1}</b><div><strong>${l.name}</strong><span>${l.note}</span></div>`;
    const on = () => { wrap.dataset.hot = l.id; if (l.focus) ctx.stage.focus(l.focus); };
    const off = () => { delete wrap.dataset.hot; ctx.stage.focus(null); };
    li.addEventListener('pointerenter', on); li.addEventListener('focus', on);
    li.addEventListener('pointerleave', off); li.addEventListener('blur', off);
    list.appendChild(li);
  });
  scene.querySelectorAll('.ex').forEach((el) => {
    el.addEventListener('pointerenter', () => { wrap.dataset.hot = el.dataset.layer; });
    el.addEventListener('pointerleave', () => { delete wrap.dataset.hot; });
  });

  const grid = section.querySelector('.softmap__grid');
  MAP.forEach((m) => {
    const c = h('article', 'softmap__card');
    c.style.setProperty('--c', m.color);
    c.innerHTML = `<h4>${m.title}</h4><p>${m.blurb}</p><ul>${m.links.map(([href, label]) => `<li><a href="${href}"><span class="mono">${href}</span><em>${label}</em></a></li>`).join('')}</ul>`;
    grid.appendChild(c);
  });
  const msgs = ctx.facts.messages?.length;
  if (msgs) grid.lastElementChild.querySelector('p').textContent += ` ${msgs} message types.`;

  const rt = section.querySelector('[data-badge="runtime"]');
  const fk = section.querySelector('[data-badge="facts"]');
  const wasm = ctx.watch.kind === 'wasm';
  rt.className = `badge ${wasm ? 'badge--live' : 'badge--off'}`;
  rt.textContent = wasm ? 'Watch: real firmware (WASM)' : 'Watch: stub preview';
  fk.className = `badge ${ctx.factsGenerated ? 'badge--live' : 'badge--off'}`;
  fk.textContent = ctx.factsGenerated ? `Facts: generated from ${String(ctx.facts.generated_from ?? 'source').slice(0, 10)}` : 'Facts not generated';

  if (!ctx.factsGenerated) {
    const f = h('div', 'badge badge--float', 'facts not generated');
    f.title = 'atlas/data/facts.json is missing. Showing built-in fallbacks. Run atlas/tools/extract.py.';
    document.body.appendChild(f);
  }

  wrap._open = (open) => wrap.classList.toggle('is-open', open);
}

export function onEnter(ctx) {
  const wrap = document.querySelector('.explode');
  if (!wrap) return;
  if (ctx.reducedMotion) wrap._open(true);
  else setTimeout(() => wrap._open(true), 220);
}
export function onLeave(ctx) {
  const wrap = document.querySelector('.explode');
  if (wrap && !ctx.reducedMotion) wrap._open(false);
  ctx.stage.focus(null);
}
