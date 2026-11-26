// Hero: the watch plays a four-beat story (work, ask, hold to approve, done + key A) while the
// page waits for a first touch. The page performs the hold and the key press on the real watch.
// The Mote (design/mote) keeps it company: the caption light is an ember until the watch shows a
// screen, and the hero's Mote drifts in by the rim, leaves on the request beat and only fades back
// once the story has moved on: never as a reward for answering (issue 20261126-b92e).

/** @typedef {{[k: string]: unknown}} Detail */
/**
 * @typedef {object} IntroCtx the parts of the shared ctx the hero uses
 * @property {{gesture?: {[k: string]: number}}} facts extracted firmware constants
 * @property {{on: (type: string, fn: (d: Detail) => void) => () => void}} bus the page's bus
 * @property {{kind: string, reset?: () => void, screen?: () => string,
 *   pointer: (type: string, x: number, y: number) => void}} watch the watch runtime
 * @property {{scenarios: {name: string, title: string}[], scenario: string | null,
 *   play: (name: string) => void} | null} daemon the mock daemon, if it loaded
 * @property {{trace: (p: {x: number, y: number, t: number}[]) => void,
 *   pressKey: (id: string, down: boolean) => void}} stage the stage around the watch
 */

const STORY = 'story';
const KEY_TAP_MS = 160;
const A_PRESS_AFTER_MS = 1800;
const BEAT_REQUEST = 1; // "!": an agent waits for you, so the Mote leaves
const BEAT_DONE = 3;

/**
 * Captions per beat; the hold time comes from the firmware.
 * @param {number} holdMs the approve hold
 * @returns {string[]} one caption per beat
 */
function captions(holdMs) {
  const s = (holdMs / 1000).toLocaleString('en', { maximumFractionDigits: 1 });
  return [
    'An agent is working.',
    'It needs approval. The watch buzzes.',
    `Hold ✓ for ${s} s to approve. A tap never does.`,
    'Done. Press A: its terminal comes forward.',
  ];
}

/**
 * Mounts the hero.
 * @param {HTMLElement} section the hero section
 * @param {IntroCtx} ctx the shared ctx
 */
export function mount(section, ctx) {
  const caption = /** @type {HTMLElement} */ (section.querySelector('#hero-caption'));
  const replay = /** @type {HTMLButtonElement} */ (section.querySelector('#hero-replay'));
  const led = /** @type {HTMLElement} */ (section.querySelector('.hero__led'));
  const mote = /** @type {HTMLElement | null} */ (section.querySelector('.hero__mote'));
  const beatEls = [...section.querySelectorAll('.hero__beats li')];
  const g = ctx.facts.gesture ?? {};
  const holdMs = g.hold_confirm_ms ?? 1200;
  const lines = captions(holdMs);
  const daemon = ctx.daemon;
  const story = daemon?.scenarios.find((s) => s.name === STORY);
  const demo = story ?? daemon?.scenarios[0];

  let inHero = true;
  let approved = false;
  let away = false; // the story is between its request and done: no Mote
  /** @type {Set<ReturnType<typeof setTimeout>>} */
  const timers = new Set();
  /**
   * @param {() => void} fn what to run
   * @param {number} ms after how long
   */
  const later = (fn, ms) => {
    const h = setTimeout(() => {
      timers.delete(h);
      fn();
    }, ms);
    timers.add(h);
  };
  const cancel = () => {
    for (const h of timers) clearTimeout(h);
    timers.clear();
  };

  /** @param {number} beat -1 lights none */
  const light = (beat) => {
    beatEls.forEach((li, i) => {
      li.toggleAttribute('data-on', i === beat);
      li.toggleAttribute('data-past', beat >= 0 && i < beat);
    });
  };

  /**
   * Moves the hero's Mote; the CSS draws each state (chapters.css .hero__mote).
   * @param {'in' | 'away' | 'back'} state where it is
   */
  const moteTo = (state) => {
    if (!mote?.dataset.state) return; // not arrived yet: the first screen brings it
    if (state === 'back' && mote.dataset.state !== 'away') return;
    mote.dataset.state = state;
  };

  // The page drives the watch only while the hero is on screen and the story is the scenario.
  const driving = () => inHero && daemon?.scenario === STORY && ctx.watch.kind === 'wasm';

  function hold() {
    if (!driving() || ctx.watch.screen?.() !== 'ask') return;
    const x = g.ask_hold_x_px ?? 304;
    const y = g.ask_hold_y_px ?? 262;
    const ms = holdMs + 500; // margin over the firmware hold
    // a small ring where the finger rests, drawn as a trail
    ctx.stage.trace(
      Array.from({ length: 24 }, (_, i) => ({
        x: x + 9 * Math.cos(i / 3.5),
        y: y + 9 * Math.sin(i / 3.5),
        t: (i * ms) / 24,
      })),
    );
    ctx.watch.pointer('down', x, y);
    for (let t = 100; t < ms; t += 100) later(() => ctx.watch.pointer('move', x, y), t);
    later(() => {
      ctx.watch.pointer('up', x, y);
      later(() => {
        if (!approved && daemon?.scenario === STORY) caption.textContent = 'The hold missed.';
      }, 600);
    }, ms);
  }

  function pressA() {
    if (!driving()) return;
    ctx.stage.pressKey('a', true);
    later(() => ctx.stage.pressKey('a', false), KEY_TAP_MS);
  }

  ctx.bus.on('scenario:step', (s) => {
    if (s.name !== STORY) {
      light(-1);
      away = false;
      moteTo('back');
      if (typeof s.text === 'string' && s.text) caption.textContent = s.text;
      return;
    }
    const beat = Number(s.step);
    if (beat > 3) return;
    light(beat);
    caption.textContent = lines[beat];
    if (beat === BEAT_REQUEST) away = true;
    else if (beat === 0 || beat === BEAT_DONE) away = false;
    moteTo(away ? 'away' : 'back');
    if (beat === 0) approved = false;
    if (beat === 2) hold();
    if (beat === 3) later(pressA, A_PRESS_AFTER_MS);
  });
  ctx.bus.on('watch:tx', (d) => {
    const msg = /** @type {{t?: string, b?: {op?: string}} | undefined} */ (d.msg);
    if (msg?.t === 'cmd' && msg.b?.op === 'approve') approved = true;
  });
  // The page is ready once every chapter is mounted; the real watch has booted by then. A stub
  // watch keeps the ember: the firmware is not running.
  const wake = () => {
    if (ctx.watch.kind === 'wasm') led.removeAttribute('data-ember');
    if (mote && !mote.dataset.state) mote.dataset.state = away ? 'away' : 'in';
  };
  ctx.bus.on('atlas:ready', wake);
  ctx.bus.on('watch:screen', wake);
  ctx.bus.on('chapter:enter', (d) => {
    inHero = d.id === 'intro';
  });

  const start = () => {
    cancel();
    light(-1);
    if (demo) daemon?.play(demo.name);
  };

  if (demo) {
    caption.textContent = demo.title;
    start();
  } else {
    caption.textContent =
      ctx.watch.kind === 'stub'
        ? 'Watch preview only: the firmware build is not loaded yet.'
        : 'Tap the watch to wake it.';
    replay.hidden = true;
    section.querySelector('.hero__beats')?.setAttribute('hidden', '');
  }
  replay.addEventListener('click', () => {
    ctx.watch.reset?.();
    start();
  });
}
