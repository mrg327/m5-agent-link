// "What it shows": the ring anatomy, state chips (colour + shape + motion) and the link indicator.
// Anchors are canvas coordinates (466 px) of the real parts of the ring screen.
const mid = 233;
const polar = (deg, r) => ({ x: Math.round(mid + r * Math.sin((deg * Math.PI) / 180)), y: Math.round(mid - r * Math.cos((deg * Math.PI) / 180)) });
const PARTS = (f) => [
  { id: 'segments', name: 'Segments', text: 'One arc per session, in the order they started. Scrub the rim to pick one; the selected one grows a halo.', how: ['scrub rim'], at: { ...polar(60, 205), text: 'a session' } },
  { id: 'disc', name: 'Centre disc', text: `The selected session, one line each: agent, repo, state word and how long it has been in it. A repo name wider than ${f.ring.repo_w_px} px drops from ${f.ring.repo_px} to ${f.ring.repo_small_px} px type before it is cut with dots. Tap the disc to act.`, how: ['tap centre'], at: { x: mid, y: 205, text: 'selected session' } },
  { id: 'pill', name: 'Answer button', text: `Only when a session needs you: ${f.ring.pills.approve}. It opens the request; a tap never approves. Button A is ${f.ring.pills.default} otherwise.`, how: ['tap', 'hold'], at: { x: mid, y: 330, text: 'default action' } },
  { id: 'hints', name: 'Edge hints', text: `Beside the buttons: A is ${f.ring.edge_hints.a} (A twice jumps to whoever needs you next), B is ${f.ring.edge_hints.b}. They vanish when there is only one session.`, how: ['A', 'B'], at: { ...polar(f.ring.hint_a_deg, f.ring.hint_r_px), text: `A · ${f.ring.edge_hints.a}` } },
  { id: 'merge', name: 'Merged Done', text: `From ${f.ring.merge_at} sessions, finished ones fold into one dim segment so the live ones keep room. Select it and they spread out again.`, how: [`${f.ring.merge_at}+`], at: { ...polar(330, 205), text: 'merged Done' } },
  { id: 'empty', name: 'Empty state', text: 'No sessions: a dim circle that says "No sessions" and why. The link line tells you whether anything is connected at all.', how: [], at: { x: mid, y: 233, text: 'no sessions' } },
];
const KINDS = [
  ['working', 'Working', 'work'], ['ask', 'Needs input', 'ask'], ['permission', 'Permission', 'perm'],
  ['done', 'Done', 'done'], ['error', 'Error', 'error'], ['idle', 'Idle', 'idle'],
];
const LINKS = [
  { kind: null,  name: 'Offline', color: 'var(--c-idle)', text: 'No host. The centre says so and the link line goes quiet.' },
  { kind: 'usb', name: 'USB',     color: 'var(--usb)',    text: 'Cable plugged in. Steady, instant.' },
  { kind: 'ble', name: 'BLE',     color: 'var(--ble)',    text: 'Wireless. Same messages, fragmented to fit.' },
];

const h = (tag, cls, html) => { const n = document.createElement(tag); if (cls) n.className = cls; if (html != null) n.innerHTML = html; return n; };
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

export function mount(section, ctx) {
  const f = ctx.facts;
  const states = KINDS.map(([pal, label, kind]) => [kind, { color: f.palette[pal], label, shape: f.ring.kinds?.[kind] }]);
  const chips = section.querySelector('[data-slot="chips"]');
  const screens = section.querySelector('[data-slot="screens"]');
  const link = section.querySelector('[data-slot="link"]');
  let selected = null;

  function showState(id, spec, sticky) {
    ctx.stage.setGlow(spec.color, .26, sticky ? 5000 : 1800);
    ctx.stage.annotate('state', { x: mid, y: 58, text: `${spec.label ?? id}` });
    ctx.bus.emit('glance:state', { id, ...spec });
  }
  const clear = () => { if (!selected) ctx.stage.annotate('state', null); };

  states.forEach(([id, spec]) => {
    const b = h('button', 'state-chip');
    b.type = 'button';
    b.style.setProperty('--c', spec.color);
    b.setAttribute('aria-pressed', 'false');
    b.innerHTML = `<i class="state-chip__dot" aria-hidden="true"></i><span class="state-chip__name">${esc(spec.label ?? id)}</span>${spec.shape ? `<small class="state-chip__src">${esc(spec.shape)}</small>` : ''}`;
    b.addEventListener('pointerenter', () => showState(id, spec, false));
    b.addEventListener('focus', () => showState(id, spec, false));
    b.addEventListener('pointerleave', clear);
    b.addEventListener('blur', clear);
    b.addEventListener('click', () => {
      const on = selected !== id;
      selected = on ? id : null;
      chips.querySelectorAll('.state-chip').forEach((c) => c.setAttribute('aria-pressed', String(on && c === b)));
      if (on) showState(id, spec, true); else ctx.stage.annotate('state', null);
    });
    chips.appendChild(b);
  });

  PARTS(f).forEach((p, i) => {
    const c = h('article', 'screen-card');
    c.tabIndex = 0;
    c.innerHTML = `<span class="screen-card__n mono">0${i + 1}</span><h3>${p.name}</h3><p>${p.text}</p>${p.how.length ? `<p class="screen-card__how">${p.how.map((k) => `<kbd class="kbd">${esc(k)}</kbd>`).join(' ')}</p>` : ''}`;
    const on = () => ctx.stage.annotate('part', p.at);
    const off = () => ctx.stage.annotate('part', null);
    c.addEventListener('pointerenter', on); c.addEventListener('focus', on);
    c.addEventListener('pointerleave', off); c.addEventListener('blur', off);
    screens.appendChild(c);
  });

  const group = h('div', 'segmented');
  group.setAttribute('role', 'radiogroup');
  group.setAttribute('aria-label', 'Link indicator');
  const note = h('p', 'segmented__note');
  LINKS.forEach((l, i) => {
    const b = h('button', 'segmented__opt', `<i style="background:${l.color}" aria-hidden="true"></i>${l.name}`);
    b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('aria-checked', String(i === 0));
    b.addEventListener('click', () => {
      group.querySelectorAll('button').forEach((x) => x.setAttribute('aria-checked', String(x === b)));
      note.textContent = l.text;
      for (const k of ['usb', 'ble']) ctx.watch.setLinkUp?.(k, k === l.kind);
      if (l.kind) ctx.stage.pulse(l.kind);
    });
    group.appendChild(b);
  });
  note.textContent = LINKS[0].text;
  link.append(h('h3', 'chapter__sub', 'Link indicator'), group, note);
}

export function onLeave(ctx) { ctx.stage.annotate('state', null); ctx.stage.annotate('part', null); }
