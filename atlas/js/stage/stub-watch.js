// Shell-owned fallback used only when atlas/js/watch-runtime.js can't load.
// It is honest: a quiet placeholder, no firmware behaviour is faked.
export function createStubWatch(canvas, { bus } = {}) {
  const ctx = canvas.getContext('2d');
  let t = 0, raf = 0, press = null;
  const rings = [];

  function frame(now) {
    t = now / 1000;
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, 466, 466);
    // slow concentric breathing: the screen is "off", not broken
    for (let i = 0; i < 4; i++) {
      const p = (t / 6 + i / 4) % 1;
      ctx.strokeStyle = `rgba(120,150,210,${(0.16 * (1 - p)).toFixed(3)})`;
      ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.arc(233, 233, 30 + p * 190, 0, Math.PI * 2); ctx.stroke();
    }
    for (let i = rings.length - 1; i >= 0; i--) {
      const r = rings[i], a = 1 - (now - r.t0) / 700;
      if (a <= 0) { rings.splice(i, 1); continue; }
      ctx.strokeStyle = `rgba(190,210,255,${(a * .6).toFixed(3)})`;
      ctx.beginPath(); ctx.arc(r.x, r.y, 8 + (1 - a) * 50, 0, Math.PI * 2); ctx.stroke();
    }
    ctx.textAlign = 'center';
    ctx.fillStyle = 'rgba(230,237,243,.82)';
    ctx.font = '600 22px ui-sans-serif, system-ui, sans-serif';
    ctx.fillText('Watch preview', 233, 222);
    ctx.fillStyle = 'rgba(139,148,158,.9)';
    ctx.font = '15px ui-monospace, Menlo, monospace';
    ctx.fillText('firmware build not loaded', 233, 250);
    ctx.fillText('stub runtime', 233, 272);
    raf = requestAnimationFrame(frame);
  }
  raf = requestAnimationFrame(frame);

  const txHandlers = new Set();
  return {
    kind: 'stub',
    pointer(type, x, y) {
      if (type === 'down') { press = { x, y, t: performance.now() }; rings.push({ x, y, t0: performance.now() }); }
      if (type === 'up' && press) { bus?.emit('watch:gesture', { kind: 'tap', x, y }); press = null; }
    },
    button(id, down) { if (down) bus?.emit('watch:gesture', { kind: `btn_${id}`, x: 0, y: 0 }); },
    deliver() {},
    onTx(fn) { txHandlers.add(fn); },
    setLinkUp() {},
    reset() {},
    destroy() { cancelAnimationFrame(raf); },
  };
}
