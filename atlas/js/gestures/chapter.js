// "touch" chapter: how you talk to it. Gesture lab, timing scope, haptic seismograph,
// gesture → action matrix and a teaser for the one-gesture Ring UI proposal.
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
    'Touch the pad, or the watch itself. The classifier below is the firmware’s own rule set, replayed live: ',
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
    block('B', 'Haptic seismograph', 'Seven patterns, one motor. This is what each buzz looks like, and what it means.', hapRoot),
    block('C', 'What each gesture does', 'Hover a cell and the real firmware performs it on the watch.', mxRoot),
    h('aside', { class: 'tc__next' },
      h('span', { class: 'tc__next-k mono', text: 'Coming next' }),
      h('p', null, 'One gesture to act: a ring UI that replaces glance → list → detail is being designed. ',
        h('a', { href: 'docs/design/UI-RING.md', text: 'Read the proposal →' }))));
  bodySlot(section).append(root);

  lab = createLab(labRoot, ctx);
  createSeismograph(hapRoot, ctx);
  createMatrix(mxRoot, ctx, lab);
  lab.attachWatch();
  requestAnimationFrame(() => lab.resize());
}

export function onEnter(ctx) {
  lab?.attachWatch();
  lab?.resize();
  try { ctx.stage?.focus?.('screen'); } catch { /* optional */ }
}

export function onLeave(ctx) {
  try { ctx.stage?.focus?.(null); } catch { /* optional */ }
}
