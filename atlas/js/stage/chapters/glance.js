// "What it shows": states as interactive chips, the screens, the link indicator.
const SCREENS = [
  { id: 'glance', name: 'Glance', text: 'One ring, one colour. Which agent needs you most, at a glance.', how: ['A'] },
  { id: 'list',   name: 'List',   text: 'Every session, most urgent first. Swipe to move, tap to open.', how: ['swipe ↑ / ↓'] },
  { id: 'detail', name: 'Detail', text: 'Repo, branch, what it is doing. Double tap brings its terminal forward.', how: ['tap', 'double tap'] },
  { id: 'toast',  name: 'Toasts', text: 'Approvals and questions slide in on top of whatever you were doing.', how: ['tap'] },
];
const LINKS = [
  { kind: null,  name: 'Offline', color: 'var(--c-idle)', text: 'No host. The ring dims and says so.' },
  { kind: 'usb', name: 'USB',     color: 'var(--usb)',    text: 'Cable plugged in. Steady, instant.' },
  { kind: 'ble', name: 'BLE',     color: 'var(--ble)',    text: 'Wireless. Same messages, fragmented to fit.' },
];

const h = (tag, cls, html) => { const n = document.createElement(tag); if (cls) n.className = cls; if (html != null) n.innerHTML = html; return n; };
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

export function mount(section, ctx) {
  const states = Object.entries(ctx.facts.states ?? {});
  const chips = section.querySelector('[data-slot="chips"]');
  const screens = section.querySelector('[data-slot="screens"]');
  const link = section.querySelector('[data-slot="link"]');
  let selected = null;

  function showState(id, spec, sticky) {
    ctx.stage.setGlow(spec.color, .26, sticky ? 5000 : 1800);
    ctx.stage.annotate('state', { x: 233, y: 58, text: `${spec.label ?? id}` });
    // a daemon that knows how to stage a state can do so; otherwise the annotation + glow carry it
    ctx.daemon?.showState?.(id);
    ctx.bus.emit('glance:state', { id, ...spec });
  }
  const clear = () => { if (!selected) ctx.stage.annotate('state', null); };

  states.forEach(([id, spec]) => {
    const b = h('button', 'state-chip');
    b.type = 'button';
    b.style.setProperty('--c', spec.color);
    b.setAttribute('aria-pressed', 'false');
    b.innerHTML = `<i class="state-chip__dot" aria-hidden="true"></i><span class="state-chip__name">${esc(spec.label ?? id)}</span>${spec.source ? `<small class="mono state-chip__src">${esc(String(spec.source).split('/').pop())}</small>` : ''}`;
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

  SCREENS.forEach((s, i) => {
    const c = h('article', 'screen-card');
    c.innerHTML = `<span class="screen-card__n mono">0${i + 1}</span><h3>${s.name}</h3><p>${s.text}</p><p class="screen-card__how">${s.how.map((k) => `<kbd class="kbd">${k}</kbd>`).join(' ')}</p>`;
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

export function onLeave(ctx) { ctx.stage.annotate('state', null); }
