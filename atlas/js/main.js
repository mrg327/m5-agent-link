// The Atlas shell: builds ctx (facts, bus, stage, watch, daemon), mounts chapters,
// and drives the stage from scroll position.
import { createBus } from './bus.js';
import { createStage } from './stage/stage.js';
import { createStubWatch } from './stage/stub-watch.js';
import * as intro from './stage/chapters/intro.js';
import * as glance from './stage/chapters/glance.js';
import * as build from './stage/chapters/build.js';

if (!document.documentElement.classList.contains('is-file')) boot();

const FALLBACK_FACTS = {
  generated_from: 'fallback',
  palette: { bg: '#000000', surface: '#0d1117', text: '#e6edf3', muted: '#8b949e', working: '#56b4e9', ask: '#f0e442', permission: '#e69f00', done: '#009e73', error: '#d55e00', idle: '#999999', amber: '#e69f00', danger: '#d55e00', success: '#009e73', accent: '#56b4e9' },
  // TODO(fact): fallback only; the real list is extracted from ui_theme.c
  states: {
    work: { color: '#56b4e9', label: 'Working' },
    input: { color: '#f0e442', label: 'Needs input' },
    error: { color: '#d55e00', label: 'Error' },
    done: { color: '#009e73', label: 'Done' },
    idle: { color: '#999999', label: 'Idle' },
  },
  display: { width: 466, height: 466, shape: 'round' },
  messages: [],
  sources: {},
};

const CHAPTER_MODULES = {
  intro, glance, build,
  flow: () => import('./flow/chapter.js'),
  touch: () => import('./gestures/chapter.js'),
  sound: () => import('./sound/chapter.js'),
  replay: () => import('./replay/chapter.js'),
};

function withTimeout(p, ms, what) {
  return Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error(`${what} timed out`)), ms))]);
}

async function loadFacts() {
  try {
    const r = await fetch('atlas/data/facts.json', { cache: 'no-cache' });
    if (!r.ok) throw new Error(String(r.status));
    return { facts: await r.json(), generated: true };
  } catch (e) {
    console.info('[atlas] facts.json unavailable, using fallbacks', e.message);
    return { facts: FALLBACK_FACTS, generated: false };
  }
}

async function boot() {
  const bus = createBus();
  const mq = matchMedia('(prefers-reduced-motion: reduce)');
  const ctx = { facts: null, bus, watch: null, daemon: null, stage: null, reducedMotion: mq.matches, factsGenerated: false };
  mq.addEventListener?.('change', (e) => { ctx.reducedMotion = e.matches; });
  window.atlas = ctx; // handy for poking from the console

  const { facts, generated } = await loadFacts();
  ctx.facts = facts;
  ctx.factsGenerated = generated;

  ctx.stage = createStage(document.getElementById('stage'), { bus, reducedMotion: ctx.reducedMotion });

  try {
    const rt = await withTimeout(import('./watch-runtime.js'), 15000, 'watch runtime');
    ctx.watch = await withTimeout(rt.createWatch(ctx.stage.canvas, { bus }), 15000, 'createWatch');
  } catch (e) {
    console.info('[atlas] using stub watch:', e.message);
    ctx.watch = createStubWatch(ctx.stage.canvas, { bus });
  }
  ctx.stage.attachWatch(ctx.watch);

  try {
    const md = await import('./proto/mock-daemon.js');
    ctx.daemon = md.createMockDaemon({ bus, facts });
    ctx.daemon.attach(ctx.watch);
    ctx.watch.setLinkUp?.('ws', true); // the page's own host link: the watch then says hello
  } catch (e) {
    console.info('[atlas] no mock daemon:', e.message);
  }

  await mountChapters(ctx);
  wireScroll(ctx);
  document.documentElement.dataset.atlas = 'ready';
  bus.emit('atlas:ready', { watch: ctx.watch.kind, daemon: !!ctx.daemon });
}

async function mountChapters(ctx) {
  const mods = {};
  for (const section of document.querySelectorAll('[data-chapter]')) {
    const id = section.dataset.chapter;
    let mod = CHAPTER_MODULES[id];
    try {
      if (typeof mod === 'function') mod = await mod();
      if (!mod?.mount) continue;
      await mod.mount(section, ctx);
      mods[id] = mod;
    } catch (e) {
      console.info(`[atlas] chapter "${id}" not mounted:`, e.message);
      section.dataset.unavailable = '';
    }
  }
  ctx.chapters = mods;
}

function wireScroll(ctx) {
  const sections = [...document.querySelectorAll('[data-chapter]')];
  const links = new Map([...document.querySelectorAll('[data-nav]')].map((a) => [a.dataset.nav, a]));
  let current = null;

  function enter(section) {
    const id = section.dataset.chapter;
    if (current) ctx.chapters?.[current.dataset.chapter]?.onLeave?.(ctx);
    current = section;
    for (const [k, a] of links) a.setAttribute('aria-current', String(k === id));
    const layout = section.dataset.layout;
    document.body.dataset.layout = layout || '';
    ctx.stage.setMode(layout === 'hero' ? 'hero' : layout === 'side' ? 'side' : 'mini');
    ctx.stage.focus(null);
    ctx.chapters?.[id]?.onEnter?.(ctx);
    ctx.bus.emit('chapter:enter', { id });
  }

  const io = new IntersectionObserver((entries) => {
    const hit = entries.filter((e) => e.isIntersecting).sort((a, b) => b.intersectionRatio - a.intersectionRatio)[0];
    if (hit && hit.target !== current) enter(hit.target);
  }, { rootMargin: '-42% 0px -52% 0px', threshold: [0, 1] });
  sections.forEach((s) => io.observe(s));
  enter(sections[0]);

  const bar = document.getElementById('progress');
  let ticking = false;
  const update = () => {
    ticking = false;
    const max = document.documentElement.scrollHeight - innerHeight;
    bar.style.setProperty('--p', max > 0 ? (scrollY / max).toFixed(4) : '0');
  };
  addEventListener('scroll', () => { if (!ticking) { ticking = true; requestAnimationFrame(update); } }, { passive: true });
  update();
}
