// "touch" chapter: how you talk to it. Gesture lab, timing scope, haptic seismograph,
// gesture → action matrix.
import { h, bodySlot } from './dom.js';
import { createLab } from './lab.js';
import { createSeismograph } from './haptics.js';
import { createMatrix } from './matrix.js';

let lab = null;

function block(num, title, lead, ...kids) {
  return h('section', { class: 'tc__block' },
    h('header', { class: 'tc__bh' }, h('span', { class: 'tc__num mono', text: num }), h('h3', { class: 'tc__t', text: title })),
    lead ? h('p', { class: 'tc__lead', text: lead }) : null, ...kids);
}

export function mount(section, ctx) {
  const root = h('div', { class: 'tc' });
  const f = ctx.facts, g = f.gesture;

  const intro = h('p', { class: 'tc__intro' },
    'One screen, one ring. Scrub the rim to pick a session, tap the centre to act, hold it for ',
    h('b', { class: 'mono', text: `${g.hold_confirm_ms} ms` }), ' to approve, swipe down to deny. The classifier below is the firmware’s own rule set: ',
    h('b', { class: 'mono', text: `${g.tap_slop_px} px` }), ' slop, ',
    h('b', { class: 'mono', text: `${g.swipe_min_px} px` }), ' in ',
    h('b', { class: 'mono', text: `${g.swipe_max_ms} ms` }), ' for a swipe, ',
    h('b', { class: 'mono', text: `${g.long_press_ms} ms` }), ' for a long press.');
  root.append(intro);

  const labRoot = h('div');
  const hapRoot = h('div');
  const mxRoot = h('div');
  root.append(
    block('A', 'Gesture lab', null, labRoot),
    block('B', 'Haptic seismograph', 'Ten patterns, one motor. This is what each buzz looks like, and what it means.', hapRoot),
    block('C', 'What each gesture does', 'Hover a cell and the real firmware performs it on the watch.', mxRoot),
    h('aside', { class: 'tc__next' },
      h('span', { class: 'tc__next-k mono', text: 'Shipped' }),
      h('p', null, 'The ring replaced glance → list → detail: one screen, and every action is one gesture. ',
        h('a', { href: 'docs/design/UI-RING.md', text: 'Read the design notes →' }))));
  bodySlot(section).append(root);

  lab = createLab(labRoot, ctx);
  createSeismograph(hapRoot, ctx);
  createMatrix(mxRoot, ctx);
  lab.attachWatch?.();
  requestAnimationFrame(() => lab.resize());
}

export function onEnter(ctx) {
  lab?.attachWatch?.();
  lab?.resize();
  try { ctx.stage?.focus?.('screen'); } catch { /* optional */ }
}

export function onLeave(ctx) {
  try { ctx.stage?.focus?.(null); } catch { /* optional */ }
}
