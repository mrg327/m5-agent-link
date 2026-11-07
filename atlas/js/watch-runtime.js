// WatchRuntime: the real firmware UI core + LVGL (atlas/wasm → atlas/vendor/watch.{js,wasm}) on a 466×466 canvas.
// Falls back to an honest placeholder ("stub") when the WebAssembly build is missing.
const SIZE = 466;
const LINK_KINDS = { usb: 0, ble: 1, ws: 2 };
const GESTURE_KINDS = {
  tap: 'tap', double_tap: 'double_tap', long_press: 'long_press',
  swipe_up: 'swipe_u', swipe_down: 'swipe_d', swipe_left: 'swipe_l', swipe_right: 'swipe_r',
  btn_a: 'btn_a', btn_b: 'btn_b', btn_a_long: 'btn_a', btn_b_long: 'btn_b',
};

const noop = () => {};

function makeEmitter(bus) {
  return (type, detail) => { try { bus?.emit(type, detail); } catch (e) { console.error(e); } };
}

// The firmware's debug channel is one JSON object per line (see hal_debug_line); lift gestures and screens out of it.
function handleDebugLine(line, emit, log) {
  log?.(line);
  if (line[0] !== '{') return;
  let ev;
  try { ev = JSON.parse(line); } catch { return; }
  if (ev.cat === 'input' && GESTURE_KINDS[ev.ev]) {
    emit('watch:gesture', { kind: GESTURE_KINDS[ev.ev], x: ev.x ?? 0, y: ev.y ?? 0, long: ev.ev.endsWith('_long') || undefined });
  } else if (ev.cat === 'ui' && ev.ev === 'screen') {
    emit('watch:screen', { screen: ev.screen, sel: ev.sel_sid ?? 0 });
  }
}

export async function createWatch(canvas, { bus, autoStart = true, onDebug, moduleUrl } = {}) {
  const emit = makeEmitter(bus);
  canvas.width = SIZE;
  canvas.height = SIZE;
  try {
    const url = moduleUrl ?? new URL('../vendor/watch.js', import.meta.url).href;
    const factory = (await import(url)).default;
    return await createWasmWatch(factory, canvas, emit, autoStart, onDebug);
  } catch (err) {
    console.warn('[watch] WebAssembly firmware unavailable, using stub:', err);
    return createStubWatch(canvas, emit);
  }
}

async function createWasmWatch(factory, canvas, emit, autoStart, onDebug) {
  const ctx2d = canvas.getContext('2d', { alpha: false });
  let txFns = [];
  let M = null;
  let imageData = null;
  let imageBuf = null;
  let queue = [];
  let raf = 0;
  let running = false;
  let stats = { ticks: 0, totalMs: 0, maxMs: 0, last: 0 };
  let linkState = { usb: false, ble: false, ws: false };

  const hooks = {
    tx: (_kind, bytes) => txFns.forEach((f) => f(bytes)),
    haptic: (ms) => {
      const steps = [];
      for (let i = 0; i < ms.length; i += 2) steps.push({ on_ms: ms[i], off_ms: ms[i + 1] ?? 0, duty: 100 });
      emit('watch:haptic', { steps });
    },
    debug: (line) => handleDebugLine(line, emit, onDebug),
  };

  function boot() {
    return factory({ hooks, print: noop, printErr: (s) => console.warn('[watch]', s) }).then((m) => {
      M = m;
      M._wasm_set_wallclock(Math.floor(Date.now() / 1000));
      M._wasm_init();
      imageData = null;
      for (const [k, up] of Object.entries(linkState)) if (up) M._wasm_link_up(LINK_KINDS[k], 1);
    });
  }

  function blit() {
    if (!M._wasm_take_dirty()) return;
    const buf = M.HEAPU8.buffer;
    if (!imageData || imageBuf !== buf) {
      imageBuf = buf;
      imageData = new ImageData(new Uint8ClampedArray(buf, M._wasm_fb_ptr(), SIZE * SIZE * 4), SIZE, SIZE);
    }
    const d = new Int32Array(buf, M._wasm_dirty_ptr(), 4);
    const x = d[0], y = d[1];
    ctx2d.putImageData(imageData, 0, 0, x, y, d[2] - x + 1, d[3] - y + 1);
  }

  function applyInput() {
    // One pointer sample per tick so a fast down+up still reaches the core as two distinct reads.
    let ev = queue.shift();
    while (ev && ev.t === 'move' && queue.length && queue[0].t === 'move') ev = queue.shift();
    if (!ev) return;
    if (ev.t === 'btn') M._wasm_button(ev.id, ev.down ? 1 : 0);
    else M._wasm_pointer(ev.t === 'up' ? 0 : 1, ev.x, ev.y);
  }

  function step() {
    const t0 = performance.now();
    applyInput();
    M._wasm_tick();
    blit();
    const dt = performance.now() - t0;
    stats.ticks++;
    stats.totalMs += dt;
    stats.maxMs = Math.max(stats.maxMs, dt);
    stats.last = dt;
  }

  function loop() {
    if (!running) return;
    step();
    raf = requestAnimationFrame(loop);
  }

  await boot();
  step();

  const watch = {
    kind: 'wasm',
    canvas,
    pointer(type, x, y) {
      queue.push({ t: type, x: Math.round(x), y: Math.round(y) });
      if (queue.length > 64) queue.splice(0, queue.length - 64);
    },
    button(id, down) { queue.push({ t: 'btn', id: id === 'b' ? 1 : 0, down: !!down }); },
    onTx(fn) {
      txFns.push(fn);
      return () => { txFns = txFns.filter((f) => f !== fn); };
    },
    setLinkUp(kind, up) {
      if (!(kind in LINK_KINDS)) return;
      linkState[kind] = !!up;
      M._wasm_link_up(LINK_KINDS[kind], up ? 1 : 0);
    },
    // Deliver on a specific transport (default 'ws').
    deliverOn(kind, bytes) {
      const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
      if (u8.length === 0 || u8.length > 4096) return false;
      M.HEAPU8.set(u8, M._wasm_rx_buf());
      M._wasm_deliver(LINK_KINDS[kind] ?? 2, u8.length);
      return true;
    },
    async reset() {
      const was = running;
      running = false;
      cancelAnimationFrame(raf);
      queue = [];
      await boot(); // fresh module instance == power cycle
      step();
      if (was) watch.start();
    },
    start() {
      if (running) return;
      running = true;
      raf = requestAnimationFrame(loop);
    },
    stop() { running = false; cancelAnimationFrame(raf); },
    step(n = 1) { for (let i = 0; i < n; i++) step(); },
    screen() { return M.UTF8ToString(M._wasm_screen_name()); },
    stats() { return { ...stats, avgMs: stats.ticks ? stats.totalMs / stats.ticks : 0, fps: M._wasm_fps() }; },
    get module() { return M; },
  };
  watch.deliver = (bytes, kind = 'ws') => watch.deliverOn(kind, bytes);
  if (autoStart) watch.start();
  return watch;
}

function createStubWatch(canvas, emit) {
  const g = canvas.getContext('2d');
  const cx = SIZE / 2;
  const draw = () => {
    g.fillStyle = '#000';
    g.fillRect(0, 0, SIZE, SIZE);
    g.strokeStyle = '#3a3f47';
    g.lineWidth = 2;
    g.beginPath();
    g.arc(cx, cx, cx - 14, 0, Math.PI * 2);
    g.stroke();
    g.fillStyle = '#8b949e';
    g.textAlign = 'center';
    g.font = '300 22px -apple-system, "Helvetica Neue", sans-serif';
    g.fillText('Firmware preview unavailable', cx, cx - 6);
    g.fillStyle = '#6e7681';
    g.font = '16px ui-monospace, Menlo, monospace';
    g.fillText('run atlas/wasm/build.sh', cx, cx + 26);
  };
  draw();
  let txFns = [];
  let down = null;
  return {
    kind: 'stub',
    canvas,
    // The stub only reports raw tap/swipe geometry; it never pretends to run firmware.
    pointer(type, x, y) {
      if (type === 'down') down = { x, y, t: performance.now() };
      else if (type === 'up' && down) {
        const dx = x - down.x, dy = y - down.y;
        let kind = 'tap';
        if (Math.hypot(dx, dy) > 60) kind = Math.abs(dx) > Math.abs(dy) ? (dx > 0 ? 'swipe_r' : 'swipe_l') : dy > 0 ? 'swipe_d' : 'swipe_u';
        emit('watch:gesture', { kind, x: down.x, y: down.y, stub: true });
        down = null;
      }
    },
    button(id, isDown) { if (isDown) emit('watch:gesture', { kind: id === 'b' ? 'btn_b' : 'btn_a', x: 0, y: 0, stub: true }); },
    deliver: noop,
    deliverOn: noop,
    onTx(fn) { txFns.push(fn); return () => { txFns = txFns.filter((f) => f !== fn); }; },
    setLinkUp: noop,
    reset: async () => draw(),
    start: noop,
    stop: noop,
    step: noop,
    screen: () => 'stub',
    stats: () => ({ ticks: 0, avgMs: 0, maxMs: 0, fps: 0 }),
  };
}
