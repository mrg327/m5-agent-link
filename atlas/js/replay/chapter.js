// "replay" chapter: drop a ~/.agentlink/trace/*.jsonl and replay the real sessions on the
// stage watch. Everything is parsed in the browser; nothing is uploaded.
import { h, svg, bodySlot, fitCanvas, hexA, clamp, cssVar } from '../gestures/dom.js';
import { parseTrace, buildTimeline, stateAt } from './trace.js';

const SID_BASE = 9000; // keeps replayed sessions apart from the mock daemon's scenario sids
const SAMPLE_URL = new URL('../../data/sample-trace.jsonl', import.meta.url);
const SPEEDS = [1, 10, 60];

const fmt = (ms) => {
  const s = Math.max(0, Math.floor(ms / 1000));
  const hh = Math.floor(s / 3600), mm = Math.floor((s % 3600) / 60), ss = s % 60;
  const p = (n) => String(n).padStart(2, '0');
  return hh ? `${hh}:${p(mm)}:${p(ss)}` : `${p(mm)}:${p(ss)}`;
};

export function mount(section, ctx) {
  const f = ctx.facts;
  const root = h('div', { class: 'rp' });
  bodySlot(section).append(root);

  // ── drop zone ──
  const err = h('p', { class: 'rp__err', role: 'status' });
  const fileInput = h('input', { type: 'file', accept: '.jsonl,.json,.txt,application/json', multiple: true, hidden: true });
  const drop = h('div', { class: 'rp__drop', role: 'group', 'aria-label': 'Drop a trace file here' },
    h('p', { class: 'rp__drop-t' }, 'Drop a trace from ', h('code', { text: '~/.agentlink/trace/*.jsonl' })),
    h('div', { class: 'rp__row' },
      h('button', { type: 'button', class: 'rp__btn', 'data-primary': '1', text: 'Choose a file…', onclick: () => fileInput.click() }),
      h('button', { type: 'button', class: 'rp__btn', text: 'Try a sample', onclick: () => loadSample() })),
    h('p', { class: 'rp__priv' }, h('b', { text: 'Stays in your browser.' }),
      ' The file is read locally and never uploaded. Only folder names (not full paths) are shown.'),
    err, fileInput);
  ['dragenter', 'dragover'].forEach((t) => drop.addEventListener(t, (e) => { e.preventDefault(); drop.dataset.over = '1'; }));
  ['dragleave', 'drop'].forEach((t) => drop.addEventListener(t, (e) => { e.preventDefault(); drop.dataset.over = ''; }));
  drop.addEventListener('drop', (e) => readFiles([...e.dataTransfer.files]));
  fileInput.addEventListener('change', () => { readFiles([...fileInput.files]); fileInput.value = ''; });

  // ── deck (after load) ──
  const stats = h('div', { class: 'rp__stats' });
  const canvas = h('canvas', { class: 'rp__canvas', role: 'img', 'aria-label': 'Timeline of agent sessions as light streaks coloured by state. Drag to scrub.', tabindex: 0 });
  const clock = h('div', { class: 'rp__clock mono' }, '00:00');
  const playBtn = h('button', { type: 'button', class: 'rp__play', 'aria-label': 'Play' });
  const speedBtns = SPEEDS.map((s) => h('button', { type: 'button', class: 'seg__b', 'aria-pressed': 'false', text: s + '×', onclick: () => setSpeed(s) }));
  const now = h('div', { class: 'rp__now', 'aria-live': 'off' });
  const legend = h('div', { class: 'rp__legend' },
    ['work', 'input', 'error', 'done', 'idle'].map((k) => h('span', { style: { '--c': f.states[k].color }, text: f.states[k].label })),
    h('span', { style: { '--c': '#fff' }, text: 'focus on PC' }));
  const deck = h('div', { class: 'rp__deck', hidden: true },
    stats,
    h('div', { class: 'rp__stagewrap' }, canvas),
    h('div', { class: 'rp__ctl' }, playBtn, clock, h('div', { class: 'seg rp__speeds', role: 'group', 'aria-label': 'Playback speed' }, speedBtns)),
    now, legend,
    h('p', { class: 'rp__note', text: 'Playing feeds the same session updates to the watch on the stage, so you see your real history on the real firmware.' }));
  root.append(drop, deck);

  const setPlayIcon = (playing) => {
    playBtn.replaceChildren(playing
      ? svg('svg', { viewBox: '0 0 16 16' }, svg('rect', { x: 3, y: 2, width: 3.6, height: 12, rx: 1 }), svg('rect', { x: 9.4, y: 2, width: 3.6, height: 12, rx: 1 }))
      : svg('svg', { viewBox: '0 0 16 16' }, svg('path', { d: 'M4 2.2v11.6a.6.6 0 0 0 .9.5l9.3-5.8a.6.6 0 0 0 0-1L4.9 1.7a.6.6 0 0 0-.9.5z' })));
    playBtn.setAttribute('aria-label', playing ? 'Pause' : 'Play');
  };
  setPlayIcon(false);

  // ── model ──
  let trace = null, timeline = [], pos = 0, speed = 10, playing = false, raf = 0, last = 0, evIdx = 0;
  const injected = new Set();
  const flashes = []; // focus flashes {t(real), lane}
  let cw = 0, chh = 0, c2 = null;

  const stateColor = (st) => (f.states[st] || f.states.idle).color;
  const stateLabel = (st, sub) => {
    const s = f.states[st] || f.states.idle;
    return st === 'input' && sub != null && s.sub_labels ? s.sub_labels[String(sub)] || s.label : s.label;
  };

  function load(text, name) {
    err.textContent = '';
    let t;
    try { t = parseTrace(text); } catch (e) { err.textContent = 'Could not read that file.'; return; }
    if (!t.sessions.length) {
      err.textContent = t.rows ? 'No session updates found in that trace. Is it a ~/.agentlink/trace file?' : 'That file has no JSON lines.';
      return;
    }
    stop();
    trace = t; timeline = buildTimeline(t); pos = 0; evIdx = 0; injected.clear();
    try { ctx.daemon?.pause?.(); } catch { /* optional */ }
    deck.hidden = false;
    const sc = t.sessions.length, changes = t.sessions.reduce((a, s) => a + s.steps.length, 0);
    stats.replaceChildren(
      stat(String(sc), sc === 1 ? 'session' : 'sessions'), stat(fmt(t.duration), 'duration'),
      stat(String(changes), 'state changes'), stat(String(t.focus.length), 'focus taps'),
      name ? h('div', { class: 'rp__stat' }, h('b', { class: 'mono', style: { fontSize: '1rem', paddingTop: '.7rem' }, text: name }), h('span', { text: 'source' })) : null);
    const auto = t.duration > 30 * 60_000 ? 60 : t.duration > 5 * 60_000 ? 10 : 1;
    setSpeed(auto, true);
    requestAnimationFrame(() => { sizeCanvas(); draw(); updateNow(); deck.scrollIntoView?.({ block: 'nearest', behavior: ctx.reducedMotion ? 'auto' : 'smooth' }); });
  }
  const stat = (v, l) => h('div', { class: 'rp__stat' }, h('b', { class: 'mono', text: v }), h('span', { text: l }));

  async function readFiles(files) {
    if (!files.length) return;
    try {
      const parts = await Promise.all(files.sort((a, b) => a.name.localeCompare(b.name)).map((x) => x.text()));
      load(parts.join('\n'), files.length === 1 ? files[0].name : `${files.length} files`);
    } catch { err.textContent = 'Could not read that file.'; }
  }
  async function loadSample() {
    try {
      const r = await fetch(SAMPLE_URL);
      if (!r.ok) throw new Error(r.status);
      load(await r.text(), 'sample-trace.jsonl');
    } catch { err.textContent = 'Could not load the sample (is the page served over http?).'; }
  }

  // ── feeding the watch ──
  const unix = () => Math.floor(Date.now() / 1000);
  function patchFor(s, step, atT) {
    const p = { ...step.patch };
    p.sid = SID_BASE + s.sid;
    if (!p.ag) p.ag = s.ag; if (!p.repo) p.repo = s.repo; if (!p.br) p.br = s.br;
    p.tst = unix();
    p.t0 = unix() - Math.max(0, Math.round((atT - s.steps[0].t) / 1000));
    p.seen = false;
    return p;
  }
  function inject(p) { try { ctx.daemon?.inject?.(p); } catch (e) { console.warn('[atlas] inject failed', e); } }
  function fire(ev) {
    const s = trace.sessions.find((x) => x.sid === ev.sid);
    if (ev.type === 'patch' && s) {
      const step = s.steps.find((x) => x.t === ev.t && x.patch === ev.patch) || s.steps[0];
      injected.add(s.sid); inject(patchFor(s, step, ev.t));
    } else if (ev.type === 'gone' && s) {
      if (injected.has(s.sid)) inject({ sid: SID_BASE + s.sid, st: 'ended', tst: unix() });
    } else if (ev.type === 'focus') {
      flashes.push({ at: performance.now(), sid: ev.sid });
      ctx.bus?.emit?.('replay:focus', { sid: ev.sid });
    }
  }
  // After a seek: put every session in the state it had at that moment.
  function snapshot(t) {
    for (const s of trace.sessions) {
      const cur = stateAt(s, t);
      if (cur) { injected.add(s.sid); inject(patchFor(s, cur, t)); }
      else if (injected.has(s.sid)) inject({ sid: SID_BASE + s.sid, st: 'ended', tst: unix() });
    }
  }

  // ── transport ──
  function setSpeed(s, silent) {
    speed = s;
    speedBtns.forEach((b, i) => b.setAttribute('aria-pressed', String(SPEEDS[i] === s)));
    if (!silent) draw();
  }
  function play() {
    if (!trace) return;
    if (pos >= trace.duration) { pos = 0; evIdx = 0; }
    try { ctx.daemon?.pause?.(); } catch { /* optional */ }
    if (evIdx === 0 && pos === 0) injected.clear();
    playing = true; last = performance.now(); setPlayIcon(true);
    raf = requestAnimationFrame(tick);
  }
  function stop() { playing = false; cancelAnimationFrame(raf); setPlayIcon(false); }
  function tick(nowMs) {
    const dt = nowMs - last; last = nowMs;
    const prev = pos;
    pos = Math.min(trace.duration, pos + dt * speed);
    const abs = trace.start + pos;
    while (evIdx < timeline.length && timeline[evIdx].t <= abs) fire(timeline[evIdx++]);
    draw(); updateNow();
    if (pos >= trace.duration) { stop(); return; }
    if (playing) raf = requestAnimationFrame(tick);
    void prev;
  }
  function seek(p) {
    pos = clamp(p, 0, trace.duration);
    const abs = trace.start + pos;
    evIdx = timeline.findIndex((e) => e.t > abs);
    if (evIdx < 0) evIdx = timeline.length;
    snapshot(abs);
    draw(); updateNow();
  }
  playBtn.addEventListener('click', () => (playing ? stop() : play()));

  // scrubbing
  const GUT = () => (cw < 520 ? 84 : 150);
  const xToPos = (clientX) => {
    const r = canvas.getBoundingClientRect();
    const g = GUT();
    return clamp((clientX - r.left - g) / (r.width - g - 16), 0, 1) * trace.duration;
  };
  let scrubbing = null;
  canvas.addEventListener('pointerdown', (e) => { if (!trace) return; scrubbing = e.pointerId; canvas.setPointerCapture(e.pointerId); seek(xToPos(e.clientX)); });
  canvas.addEventListener('pointermove', (e) => { if (e.pointerId === scrubbing) seek(xToPos(e.clientX)); });
  const endScrub = (e) => { if (e.pointerId === scrubbing) scrubbing = null; };
  canvas.addEventListener('pointerup', endScrub); canvas.addEventListener('pointercancel', endScrub);
  canvas.addEventListener('keydown', (e) => {
    if (!trace) return;
    const step = Math.max(1000, trace.duration / 40);
    if (e.key === 'ArrowRight') { seek(pos + step); e.preventDefault(); }
    else if (e.key === 'ArrowLeft') { seek(pos - step); e.preventDefault(); }
    else if (e.key === ' ') { playing ? stop() : play(); e.preventDefault(); }
  });

  // ── "now" chips ──
  let nowKey = '';
  function updateNow() {
    clock.replaceChildren(fmt(pos), h('small', { text: '/ ' + fmt(trace.duration) }));
    const abs = trace.start + pos;
    const items = trace.sessions.map((s) => ({ s, cur: stateAt(s, abs) }));
    const key = items.map((i) => (i.cur ? i.cur.st + i.cur.sub : 'x')).join('|');
    if (key === nowKey) return;
    nowKey = key;
    now.replaceChildren(...items.map(({ s, cur }) => h('span', { class: 'rp__chip', 'data-off': cur ? '' : '1', style: { '--st': cur ? stateColor(cur.st) : '' } },
      h('i'), s.repo || 'session ' + s.sid, h('em', { text: cur ? stateLabel(cur.st, cur.sub) : 'not running' }))));
  }

  // ── light-streak timeline ──
  const LANE = 56, TOP = 34;
  function sizeCanvas() {
    const lanes = trace ? trace.sessions.length : 1;
    canvas.style.setProperty('--rp-h', `${TOP + lanes * LANE + 18}px`);
    const fit = fitCanvas(canvas, () => {});
    const r = fit(); c2 = r.c; cw = r.w; chh = r.h;
  }
  if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => { if (trace) { sizeCanvas(); draw(); } }).observe(canvas);

  function draw() {
    if (!trace) return;
    if (!c2) sizeCanvas();
    const c = c2, w = cw, hh = chh, g = GUT(), R = 16;
    const X = (t) => g + ((t - trace.start) / Math.max(1, trace.duration)) * (w - g - R);
    c.clearRect(0, 0, w, hh);
    c.font = '500 11px ui-monospace, Menlo, monospace';

    // time graticule
    const nice = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200].map((s) => s * 1000);
    const step = nice.find((n) => (n / trace.duration) * (w - g - R) > 70) || nice[nice.length - 1];
    c.textAlign = 'center';
    for (let t = 0; t <= trace.duration; t += step) {
      const x = X(trace.start + t);
      c.strokeStyle = 'rgba(230,237,243,0.06)'; c.beginPath(); c.moveTo(x, TOP - 8); c.lineTo(x, hh - 14); c.stroke();
      c.fillStyle = 'rgba(230,237,243,0.38)'; c.fillText(fmt(t), x, 16);
    }

    const sessions = trace.sessions;
    sessions.forEach((s, i) => {
      const y = TOP + i * LANE + LANE / 2;
      // lane label
      c.textAlign = 'left'; c.fillStyle = 'rgba(238,241,246,0.9)'; c.font = '500 13px ui-sans-serif, system-ui';
      c.fillText(truncate(c, s.repo || 'session ' + s.sid, g - 14), 4, y - 3);
      c.fillStyle = 'rgba(124,133,148,0.9)'; c.font = '500 10px ui-monospace, Menlo, monospace';
      c.fillText(s.ag, 4, y + 11);
      // long-exposure streaks
      const end = s.endedAt ?? trace.end;
      c.save(); c.globalCompositeOperation = 'lighter'; c.lineCap = 'round';
      s.steps.forEach((st, k) => {
        const a = st.t, b = Math.min(end, s.steps[k + 1] ? s.steps[k + 1].t : end);
        if (b <= a) return;
        const col = stateColor(st.st);
        const dim = st.st === 'idle' || st.st === 'ended';
        const x1 = X(a), x2 = Math.max(X(b), x1 + 2);
        c.strokeStyle = hexA(col, dim ? 0.07 : 0.14); c.lineWidth = dim ? 10 : 22; c.shadowColor = col; c.shadowBlur = dim ? 0 : 26;
        c.beginPath(); c.moveTo(x1, y); c.lineTo(x2, y); c.stroke();
        c.strokeStyle = hexA(col, dim ? 0.45 : 0.95); c.lineWidth = dim ? 1.5 : 3; c.shadowBlur = dim ? 0 : 10;
        c.beginPath(); c.moveTo(x1, y); c.lineTo(x2, y); c.stroke();
        if (!dim) { c.strokeStyle = 'rgba(255,255,255,0.55)'; c.lineWidth = 1; c.shadowBlur = 0; c.beginPath(); c.moveTo(x1, y); c.lineTo(x2, y); c.stroke(); }
        // state-change bead
        c.fillStyle = col; c.shadowColor = col; c.shadowBlur = 12; c.beginPath(); c.arc(x1, y, 3, 0, 7); c.fill();
      });
      c.restore();
    });

    // attention diamonds and focus taps
    for (const a of trace.attention) {
      const i = sessions.findIndex((s) => s.sid === a.sid); if (i < 0) continue;
      const y = TOP + i * LANE + LANE / 2 - 16, x = X(a.t);
      const col = a.kind === 'error' ? f.states.error.color : a.kind === 'done' ? f.states.done.color : f.states.input.color;
      c.fillStyle = col; c.shadowColor = col; c.shadowBlur = 8;
      c.beginPath(); c.moveTo(x, y - 4); c.lineTo(x + 3.5, y); c.lineTo(x, y + 4); c.lineTo(x - 3.5, y); c.closePath(); c.fill(); c.shadowBlur = 0;
    }
    const tnow = performance.now();
    for (const fo of trace.focus) {
      const i = sessions.findIndex((s) => s.sid === fo.sid); if (i < 0) continue;
      const y = TOP + i * LANE + LANE / 2, x = X(fo.t);
      const passed = fo.t <= trace.start + pos;
      c.strokeStyle = `rgba(255,255,255,${passed ? 0.95 : 0.5})`; c.lineWidth = 1.2; c.shadowColor = '#fff'; c.shadowBlur = passed ? 12 : 4;
      c.beginPath(); c.moveTo(x, y - 20); c.lineTo(x, y + 20); c.stroke();
      c.beginPath(); c.arc(x, y, 6, 0, 7); c.stroke(); c.shadowBlur = 0;
    }
    for (let i = flashes.length - 1; i >= 0; i--) if (tnow - flashes[i].at > 900) flashes.splice(i, 1);
    for (const fl of flashes) {
      const i = sessions.findIndex((s) => s.sid === fl.sid); if (i < 0) continue;
      const k = (tnow - fl.at) / 900, y = TOP + i * LANE + LANE / 2;
      const fe = trace.focus.find((x) => x.sid === fl.sid && x.t <= trace.start + pos);
      if (!fe) continue;
      c.strokeStyle = `rgba(255,255,255,${1 - k})`; c.lineWidth = 1.5; c.beginPath(); c.arc(X(trace.start + pos), y, 6 + k * 26, 0, 7); c.stroke();
    }

    // playhead
    const px = X(trace.start + pos);
    c.save(); c.shadowColor = '#fff'; c.shadowBlur = 14; c.strokeStyle = 'rgba(255,255,255,0.95)'; c.lineWidth = 1.4;
    c.beginPath(); c.moveTo(px, TOP - 10); c.lineTo(px, hh - 12); c.stroke(); c.restore();
    c.fillStyle = '#fff'; c.beginPath(); c.arc(px, TOP - 12, 3.5, 0, 7); c.fill();
    if (flashes.length && !playing) requestAnimationFrame(draw);
  }
  function truncate(c, str, max) {
    if (c.measureText(str).width <= max) return str;
    while (str.length > 1 && c.measureText(str + '…').width > max) str = str.slice(0, -1);
    return str + '…';
  }

  return { load };
}

export function onLeave() { /* playback keeps running by design; the user pauses it */ }
