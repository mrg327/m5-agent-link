// The persistent watch stage: builds the render, owns tilt, input wiring,
// ambient glow and the Stage API from atlas/CONTRACT.md.
import { deviceHTML, REGIONS } from './art.js';
import { createFx } from './fx.js';

const SVGNS = 'http://www.w3.org/2000/svg';
const MODES = ['hero', 'side', 'mini'];

export function createStage(host, { bus, reducedMotion = false } = {}) {
  host.innerHTML = '';
  const halo = document.createElement('div');
  halo.className = 'stage__halo';
  halo.setAttribute('aria-hidden', 'true');
  host.parentNode.insertBefore(halo, host);

  const anchor = document.createElement('div');
  anchor.className = 'stage__anchor';
  anchor.innerHTML = deviceHTML();
  host.appendChild(anchor);

  const $ = (sel) => anchor.querySelector(sel);
  const device = $('[data-device]');
  const canvas = $('.screen__canvas');
  const fx = createFx({ anchor, device, reducedMotion });
  const spot = $('.spot'), holes = $('.spot__holes'), ring = $('.spot__ring'), label = $('.spot__label');
  const glass = $('.glass__sheen');

  let mode = 'hero';
  let watch = null;
  let glowHold = 0;

  /* ---- fit: scale the hero to the viewport ------------------------------ */
  const root = document.documentElement;
  function fit() {
    const f = Math.max(.45, Math.min(1, innerHeight / 800, innerWidth / 1280));
    const m = Math.max(.5, Math.min(1, innerWidth / 640));
    root.style.setProperty('--fit', (innerWidth <= 900 ? m : f).toFixed(3));
  }
  fit();
  addEventListener('resize', fit, { passive: true });

  /* ---- tilt -------------------------------------------------------------- */
  const rest = { hero: [13, -19], side: [10, -15], mini: [6, -8] };
  const amp = { hero: 1, side: .7, mini: .2 };
  let nx = 0, ny = 0, cx = 0, cy = 0, shake = 0, raf = 0;
  addEventListener('pointermove', (e) => {
    nx = (e.clientX / innerWidth) * 2 - 1;
    ny = (e.clientY / innerHeight) * 2 - 1;
    kick();
  }, { passive: true });
  function kick() { if (!raf && !document.hidden) raf = requestAnimationFrame(tick); }
  function tick(t) {
    raf = 0;
    const [rx0, ry0] = rest[mode];
    const k = reducedMotion ? 0 : amp[mode];
    const drift = reducedMotion ? 0 : Math.sin(t / 2600) * 1.1;
    cx += (nx * k - cx) * .07; cy += (ny * k - cy) * .07;
    const rx = rx0 - cy * 10 + drift, ry = ry0 + cx * 14 + drift * .6;
    device.style.transform = `rotateX(${rx.toFixed(2)}deg) rotateY(${ry.toFixed(2)}deg)`;
    device.style.setProperty('--sheen-rot', `${(205 + ry * 3.2 - rx * 1.4).toFixed(1)}deg`);
    glass.style.setProperty('--gx', `${(-cx * 46 + 0).toFixed(1)}px`);
    glass.style.setProperty('--gy', `${(-cy * 34).toFixed(1)}px`);
    const settled = Math.abs(nx * k - cx) < .002 && Math.abs(ny * k - cy) < .002;
    if (!reducedMotion || !settled) raf = requestAnimationFrame(tick);
  }
  document.addEventListener('visibilitychange', kick);
  kick();

  /* ---- input: canvas pointer, keys ------------------------------------- */
  const local = (e) => {
    const k = 466 / (canvas.offsetWidth || 466);
    return [Math.max(0, Math.min(465, e.offsetX * k)), Math.max(0, Math.min(465, e.offsetY * k))];
  };
  let down = false;
  const trail = [];
  canvas.addEventListener('pointerdown', (e) => {
    down = true; trail.length = 0;
    canvas.setPointerCapture?.(e.pointerId);
    const [x, y] = local(e);
    trail.push({ x, y, t: performance.now() });
    watch?.pointer('down', x, y);
    e.preventDefault();
  });
  canvas.addEventListener('pointermove', (e) => {
    if (!down) return;
    const [x, y] = local(e);
    trail.push({ x, y, t: performance.now() });
    fx.live(x, y);
    watch?.pointer('move', x, y);
  });
  const end = (e) => {
    if (!down) return;
    down = false;
    const [x, y] = local(e);
    watch?.pointer('up', x, y);
  };
  canvas.addEventListener('pointerup', end);
  canvas.addEventListener('pointercancel', end);

  const keyEls = { a: $('.key--a'), b: $('.key--b') };
  const press = (id, isDown) => {
    keyEls[id].classList.toggle('is-down', isDown);
    watch?.button(id, isDown);
  };
  for (const id of ['a', 'b']) {
    const el = keyEls[id];
    el.addEventListener('pointerdown', (e) => { el.setPointerCapture?.(e.pointerId); press(id, true); e.preventDefault(); });
    el.addEventListener('pointerup', () => press(id, false));
    el.addEventListener('pointercancel', () => press(id, false));
    el.addEventListener('keydown', (e) => { if (e.key === ' ' || e.key === 'Enter') { if (!e.repeat) press(id, true); e.preventDefault(); } });
    el.addEventListener('keyup', (e) => { if (e.key === ' ' || e.key === 'Enter') press(id, false); });
  }
  const held = new Set();
  addEventListener('keydown', (e) => {
    const id = e.key.length === 1 ? e.key.toLowerCase() : '';
    if ((id !== 'a' && id !== 'b') || e.metaKey || e.ctrlKey || e.altKey || e.repeat) return;
    const t = e.target;
    if (t && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName))) return;
    held.add(id); press(id, true);
  });
  addEventListener('keyup', (e) => {
    const id = e.key.toLowerCase();
    if (held.delete(id)) press(id, false);
  });
  addEventListener('blur', () => { for (const id of held) press(id, false); held.clear(); });

  /* ---- ambient glow: sample the live screen for the dominant colour ----- */
  const probe = document.createElement('canvas');
  probe.width = probe.height = 24;
  const pctx = probe.getContext('2d', { willReadFrequently: true });
  function setGlow(color, a = .3) {
    if (color) root.style.setProperty('--glow', color);
    root.style.setProperty('--glow-a', String(a));
  }
  function sample() {
    if (performance.now() < glowHold || document.hidden) return;
    try {
      pctx.drawImage(canvas, 0, 0, 24, 24);
      const d = pctx.getImageData(0, 0, 24, 24).data;
      const buckets = new Float32Array(12 * 4); // hue bucket → [w, r, g, b]
      let lit = 0;
      for (let i = 0; i < d.length; i += 4) {
        const r = d[i], g = d[i + 1], b = d[i + 2];
        const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
        lit += mx;
        const sat = mx ? (mx - mn) / mx : 0;
        if (sat < .38 || mx < 70) continue;
        let h = mx === r ? ((g - b) / (mx - mn)) % 6 : mx === g ? (b - r) / (mx - mn) + 2 : (r - g) / (mx - mn) + 4;
        h = (h * 60 + 360) % 360;
        const k = Math.floor(h / 30) * 4, w = sat * mx;
        buckets[k] += w; buckets[k + 1] += r * w; buckets[k + 2] += g * w; buckets[k + 3] += b * w;
      }
      let best = -1, bw = 0;
      for (let k = 0; k < 48; k += 4) if (buckets[k] > bw) { bw = buckets[k]; best = k; }
      if (best >= 0 && bw > 900) {
        const w = buckets[best];
        setGlow(`rgb(${(buckets[best + 1] / w) | 0} ${(buckets[best + 2] / w) | 0} ${(buckets[best + 3] / w) | 0})`, Math.min(.26, .11 + bw / 90000));
      } else {
        setGlow('#58a6ff', .07 + Math.min(.05, lit / 24 / 24 / 255));
      }
    } catch { /* tainted or unavailable canvas: keep the current tint */ }
  }
  setInterval(sample, 450);
  bus?.on('watch:screen', () => setTimeout(sample, 140));
  bus?.on('watch:haptic', () => fx.pulse('haptic'));
  bus?.on('watch:gesture', (g) => {
    if (g?.kind === 'btn_a' || g?.kind === 'btn_b') { /* the daemon/watch drives its own visual; nothing to mirror */ }
  });

  /* ---- Stage API --------------------------------------------------------- */
  function circle(h) {
    const c = document.createElementNS(SVGNS, 'circle');
    c.setAttribute('cx', h.x); c.setAttribute('cy', h.y); c.setAttribute('r', h.r);
    return c;
  }
  const api = {
    canvas,
    get mode() { return mode; },
    attachWatch(w) { watch = w; },
    setMode(m) {
      if (!MODES.includes(m) || m === mode) return;
      mode = m;
      host.dataset.mode = m;
      document.body.dataset.stage = m;
      if (m === 'mini') api.focus(null);
      kick();
    },
    focus(region) {
      holes.replaceChildren(); ring.replaceChildren();
      const r = region && REGIONS[region];
      spot.classList.toggle('is-on', !!r);
      label.classList.toggle('is-on', !!r);
      if (!r) return;
      for (const h of r.holes) {
        holes.appendChild(circle(h));
        const c = circle({ x: h.x, y: h.y, r: h.r * .86 });
        ring.appendChild(c);
      }
      label.textContent = r.inside ? `${r.label} · inside` : r.label;
      const below = r.tag.y > 480;
      label.style.setProperty('--lx', `${r.tag.x}px`);
      label.style.setProperty('--ly', `${below ? r.tag.y - 74 : r.tag.y + (r.holes[0].r) - 6}px`);
    },
    annotate(id, spec) {
      fx.annotate(id, spec);
    },
    trace(points) { fx.trace(points); },
    pulse(kind) { fx.pulse(kind); },
    setGlow(color, a = .32, holdMs = 2200) { glowHold = performance.now() + holdMs; setGlow(color, a); },
    pressKey: press,
  };
  host.dataset.mode = mode;
  return api;
}
