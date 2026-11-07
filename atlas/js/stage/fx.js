// Effects layered on the watch: annotations, light-painting trails, pulses.
const SVGNS = 'http://www.w3.org/2000/svg';

export function createFx({ anchor, device, reducedMotion }) {
  const annos = anchor.querySelector('.screen__annos');
  const trailCanvas = anchor.querySelector('.screen__trace');
  const tctx = trailCanvas.getContext('2d');
  const svg = anchor.querySelector('.fx');
  const live = new Map();           // annotation id → element

  /* ---- annotations (canvas coordinates) ---------------------------------- */
  function annotate(id, spec) {
    live.get(id)?.remove();
    live.delete(id);
    if (!spec) return;
    const el = document.createElement('div');
    el.className = 'anno' + (spec.x > 233 ? ' anno--left' : '');
    el.style.setProperty('--ax', `${spec.x}px`);
    el.style.setProperty('--ay', `${spec.y}px`);
    const dot = document.createElement('i');
    const text = document.createElement('b');
    text.textContent = spec.text ?? '';
    el.append(dot, text);
    annos.appendChild(el);
    live.set(id, el);
  }

  /* ---- light-painting trail ---------------------------------------------- */
  const FADE_MS = 750;
  let segs = [];                    // {x0,y0,x1,y1,born}
  let loop = 0;
  let last = null;
  function addSeg(x, y, born) {
    if (last) segs.push({ x0: last.x, y0: last.y, x1: x, y1: y, born });
    last = { x, y };
    start();
  }
  function start() { if (!loop) loop = requestAnimationFrame(draw); }
  function draw(now) {
    loop = 0;
    tctx.clearRect(0, 0, 466, 466);
    segs = segs.filter((s) => now - s.born < FADE_MS);
    tctx.lineCap = 'round';
    for (const s of segs) {
      const a = 1 - (now - s.born) / FADE_MS;
      tctx.strokeStyle = `rgba(255,255,255,${(a * a * .55).toFixed(3)})`;
      tctx.lineWidth = 16 * a + 3;
      tctx.beginPath(); tctx.moveTo(s.x0, s.y0); tctx.lineTo(s.x1, s.y1); tctx.stroke();
      tctx.strokeStyle = `rgba(210,230,255,${(a * .95).toFixed(3)})`;
      tctx.lineWidth = 3.2 * a + .6;
      tctx.beginPath(); tctx.moveTo(s.x0, s.y0); tctx.lineTo(s.x1, s.y1); tctx.stroke();
    }
    if (segs.length) loop = requestAnimationFrame(draw);
    else last = null;
  }
  // the user's own finger, drawn as it moves
  function liveTrace(x, y) { if (!reducedMotion) addSeg(x, y, performance.now()); }
  // a scripted path: [{x, y, t}] with t in ms relative to the first point
  function trace(points) {
    if (!points?.length || reducedMotion) return;
    const t0 = points[0].t ?? 0, now = performance.now();
    last = null;
    points.forEach((p, i) => {
      const delay = (p.t ?? i * 16) - t0;
      setTimeout(() => addSeg(p.x, p.y, performance.now()), delay);
    });
    void now;
  }

  /* ---- pulses ------------------------------------------------------------- */
  const el = (name, attrs) => {
    const n = document.createElementNS(SVGNS, name);
    for (const k in attrs) n.setAttribute(k, attrs[k]);
    svg.appendChild(n);
    return n;
  };
  const cleanup = (node, ms) => setTimeout(() => node.remove(), ms);

  function haptic() {
    if (!reducedMotion) {
      device.animate(
        [{ translate: '0 0' }, { translate: '-5px 2px' }, { translate: '4px -2px' }, { translate: '-3px 1px' }, { translate: '2px 0' }, { translate: '0 0' }],
        { duration: 260, easing: 'ease-out' },
      );
    }
    // seismograph beneath the puck
    const w = 520, x0 = 40, y = 668;
    let d = `M${x0} ${y}`;
    for (let i = 1; i <= 26; i++) {
      const env = Math.sin((i / 26) * Math.PI);
      d += ` L${x0 + (i * w) / 26} ${y + (i % 2 ? -1 : 1) * 34 * env * (.55 + .45 * Math.sin(i * 1.7))}`;
    }
    const p = el('path', { d, class: 'seismo' });
    const len = p.getTotalLength?.() || 800;
    p.style.strokeDasharray = len;
    p.animate([{ strokeDashoffset: len, opacity: 1 }, { strokeDashoffset: 0, opacity: 1, offset: .55 }, { strokeDashoffset: 0, opacity: 0 }], { duration: 900, easing: 'ease-out' });
    cleanup(p, 920);
  }

  function ble() {
    for (let i = 0; i < 3; i++) {
      const c = el('circle', { cx: 300, cy: 70, r: 40, class: 'ripple' });
      c.animate([{ transform: 'scale(.2)', opacity: .9 }, { transform: 'scale(5.2)', opacity: 0 }], { duration: 1500, delay: i * 260, easing: 'cubic-bezier(.2,.6,.3,1)', fill: 'both' });
      cleanup(c, 1500 + i * 260 + 50);
    }
  }

  function usb() {
    const cable = el('path', { d: 'M780 300 L612 300', class: 'cable' });
    const bead = el('circle', { cx: 0, cy: 0, r: 6, class: 'bead' });
    bead.animate([{ transform: 'translate(780px,300px)', opacity: 0 }, { transform: 'translate(740px,300px)', opacity: 1, offset: .15 }, { transform: 'translate(608px,300px)', opacity: 1, offset: .9 }, { transform: 'translate(600px,300px)', opacity: 0 }], { duration: 650, easing: 'ease-in' });
    const pulse = el('circle', { cx: 598, cy: 300, r: 16, class: 'ripple', style: 'stroke:var(--usb)' });
    pulse.animate([{ transform: 'scale(.6)', opacity: 0 }, { transform: 'scale(.6)', opacity: .9, offset: .55 }, { transform: 'scale(2.6)', opacity: 0 }], { duration: 1000, easing: 'ease-out' });
    cleanup(cable, 1050); cleanup(bead, 700); cleanup(pulse, 1050);
  }

  function pulse(kind) {
    if (kind === 'haptic') haptic();
    else if (kind === 'ble') ble();
    else if (kind === 'usb') usb();
  }

  return { annotate, trace, pulse, live: liveTrace };
}
