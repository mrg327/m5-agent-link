// "touch" chapter: how you talk to it. Gesture lab, timing scope and the gesture → action matrix.
// The haptic seismograph lives in the "feel" chapter (sound/chapter.js).
import { h, bodySlot } from './dom.js';
import { createLab } from './lab.js';
import { createMatrix } from './matrix.js';

let lab = null;

function block(num, title, lead, ...kids) {
  return h(
    'section',
    { class: 'tc__block' },
    h(
      'header',
      { class: 'tc__bh' },
      h('span', { class: 'tc__num mono', text: num }),
      h('h3', { class: 'tc__t', text: title }),
    ),
    lead ? h('p', { class: 'tc__lead', text: lead }) : null,
    ...kids,
  );
}

export function mount(section, ctx) {
  const root = h('div', { class: 'tc' });
  const f = ctx.facts,
    g = f.gesture;

  const s = (/** @type {number} */ ms) => `${(ms / 1000).toFixed(1).replace(/\.0$/, '')} s`;
  const intro = h(
    'div',
    { class: 'tc__intro' },
    h(
      'p',
      null,
      'Drag round the rim to pick a session. Tap the centre to open it: its menu, or the request when one waits. To approve, hold the approve disc for ',
      h('b', { class: 'mono', text: s(g.hold_confirm_ms) }),
      '; to deny, hold the deny disc for ',
      h('b', { class: 'mono', text: s(g.deny_hold_ms) }),
      '. A tap never answers a request.',
    ),
    h(
      'p',
      null,
      'The lab below uses the firmware’s own thresholds: a tap may drift ',
      h('b', { class: 'mono', text: `${g.tap_slop_px} px` }),
      ', a swipe is ',
      h('b', { class: 'mono', text: `${g.swipe_min_px} px` }),
      ' within ',
      h('b', { class: 'mono', text: s(g.swipe_max_ms) }),
      ', a long press is ',
      h('b', { class: 'mono', text: s(g.long_press_ms) }),
      '.',
    ),
  );
  root.append(intro);

  const labRoot = h('div');
  const mxRoot = h('div');
  root.append(
    block('A', 'Gesture lab', null, labRoot),
    block(
      'B',
      'What each gesture does',
      'Pick a cell and the real firmware performs it on the watch.',
      mxRoot,
    ),
    h(
      'aside',
      { class: 'tc__next' },
      h('span', { class: 'tc__next-k mono', text: 'Design' }),
      h(
        'p',
        null,
        'The rules behind these gestures: ',
        h('a', { href: 'docs/design/UI-V3.md', text: 'UI-V3' }),
        ' (gestures and safety) and ',
        h('a', { href: 'docs/design/GLYPHS.md', text: 'GLYPHS' }),
        ' (the glyph-first look).',
      ),
    ),
  );
  bodySlot(section).append(root);

  lab = createLab(labRoot, ctx);
  createMatrix(mxRoot, ctx);
  lab.attachWatch?.();
  requestAnimationFrame(() => lab.resize());
}

export function onEnter(ctx) {
  lab?.attachWatch?.();
  lab?.resize();
  try {
    ctx.stage?.focus?.('screen');
  } catch {
    /* optional */
  }
}

export function onLeave(ctx) {
  try {
    ctx.stage?.focus?.(null);
  } catch {
    /* optional */
  }
}
