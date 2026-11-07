// Tiny DOM helpers shared by the touch and replay chapters.
export function h(tag, props, ...kids) {
  const el = tag.includes(':') ? document.createElementNS('http://www.w3.org/2000/svg', tag.slice(4)) : document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') el.setAttribute('class', v);
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k === 'style' && typeof v === 'object') {
      for (const [sk, sv] of Object.entries(v)) { if (sk.startsWith('--')) el.style.setProperty(sk, sv); else el.style[sk] = sv; }
    }
    else el.setAttribute(k, v === true ? '' : String(v));
  }
  for (const kid of kids.flat()) {
    if (kid == null || kid === false) continue;
    el.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  }
  return el;
}

export const svg = (tag, props, ...kids) => h('svg:' + tag, props, ...kids);

export function bodySlot(section) {
  return section.querySelector('[data-slot="body"]') || section;
}

export function cssVar(name, fallback) {
  const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return v || fallback;
}

export const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

export function hexA(hex, a) {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
}

// Re-run `draw` when the canvas' CSS size changes; returns {ctx2d, w, h} for the current size.
export function fitCanvas(canvas, onResize) {
  const fit = () => {
    const r = canvas.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(1, Math.round(r.width)), hgt = Math.max(1, Math.round(r.height));
    if (canvas.width !== w * dpr || canvas.height !== hgt * dpr) {
      canvas.width = w * dpr; canvas.height = hgt * dpr;
    }
    const c = canvas.getContext('2d');
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { c, w, h: hgt };
  };
  if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => onResize(fit())).observe(canvas);
  return fit;
}
