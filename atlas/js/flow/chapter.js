// Chapter 02 — "From hook to wrist": how an agent event becomes bytes on a transport and a
// light on the watch, and how a double-tap travels back as `cmd focus`.
import { createDiagram, LANE_META } from './diagram.js';
import { createInspector } from './inspector.js';
import { encodeMsg, hello, welcome, snapshot, cmd, describe } from '../proto/messages.js';

const MAX_FRAMES = 10;
const TRANSPORTS = ['usb', 'ble', 'ws'];
const WS_ADDR = '127.0.0.1:7878'; // TODO(fact): not yet in facts.json (daemon ws.rs default)

const short = (u) => (u ? u.slice(0, 8) + '…' : '?');

function tagsFor(kind, f) {
  const link = f.link ?? {};
  const common = [
    ['ping', `every ${(link.ping_interval_ms ?? 5000) / 1000} s`],
    ['silence', `${(link.silence_timeout_ms ?? 12000) / 1000} s = link lost`],
    ['hello retry', `${(link.hello_retry_ms ?? 3000) / 1000} s`],
  ];
  if (kind === 'usb') {
    const u = f.usb ?? {};
    return [['VID', u.vid], ['PID', u.pid], ['baud', u.baud], ['crc', u.crc], ['max payload', `${u.max_payload} B`], ...common];
  }
  if (kind === 'ble') {
    const b = f.ble ?? {};
    return [['name', `${b.name_prefix}*`], ['service', short(b.service)], ['rx', short(b.rx)], ['tx', short(b.tx)],
      ['write', `${b.default_write_len} B (up to ${b.max_write_len})`], ...common];
  }
  return [['endpoint', `ws://${WS_ADDR}`], ['message', 'binary'], ['framing', 'none extra'], ...common];
}

const BLURB = {
  usb: 'A cable. Steady, and the watch charges too. Frames are wrapped in COBS so a 00 byte always means “frame boundary”.',
  ble: 'Wireless, no cable. Each frame is cut into small fragments that fit one Bluetooth write.',
  ws: 'For the simulator and the browser. WebSocket already delivers whole messages.',
};

export function mount(section, ctx) {
  const { facts, bus } = ctx;
  const reduced = !!ctx.reducedMotion;
  const slot = section.querySelector('[data-slot="body"]') ?? section.querySelector('.chapter__inner') ?? section;
  const root = document.createElement('div');
  root.className = 'flow';
  slot.append(root);

  root.innerHTML = `
    <p class="flow__lede">An agent does something. A tiny hook tells the daemon. The daemon turns it into a few bytes and sends them over whichever link is up. <b>Watch the beads. Click one to open the packet.</b></p>

    <div class="flow__bar" role="group" aria-label="Scenario controls">
      <button type="button" class="fx-btn fx-btn--play" data-r="play" aria-pressed="true"><span class="fx-ico" aria-hidden="true"></span><span data-r="playTxt">Pause</span></button>
      <div class="fx-pills" role="radiogroup" aria-label="Scenario" data-r="scen"></div>
      <p class="flow__now" aria-live="polite"><i class="flow__nowdot" aria-hidden="true"></i><span data-r="now">Waiting for the first event…</span></p>
    </div>

    <div class="flow__diagram" data-r="diagram"></div>
    <p class="flow__return" data-r="ret" aria-live="polite">Double-tap the watch to send <code>cmd focus</code> back along the thread.</p>

    <div class="flow__transport">
      <div class="flow__tswitch" role="radiogroup" aria-label="Transport" data-r="tswitch"></div>
      <p class="flow__blurb" data-r="blurb"></p>
      <ul class="flow__tags" data-r="tags" aria-label="Transport facts"></ul>
      <p class="flow__rule"><b>Arbitration:</b> every transport that is up sends <code>hello</code>; the first <code>welcome</code> wins.</p>
    </div>

    <div class="flow__recent">
      <h3 class="flow__h">Recent frames <span data-r="live" class="flow__live">live</span></h3>
      <div class="flow__chips" data-r="chips" role="listbox" aria-label="Recent frames"></div>
    </div>

    <div class="pk" data-r="pk"></div>
  `;
  const r = Object.fromEntries([...root.querySelectorAll('[data-r]')].map((n) => [n.dataset.r, n]));
  const css = getComputedStyle(root);
  const cv = (name, fb) => css.getPropertyValue(name).trim() || fb;
  const laneColor = (k) => cv(`--lane-${k}`, { usb: '#58a6ff', ble: '#a371f7', ws: '#5fe0c8' }[k]);
  const retColor = () => cv('--flow-return', '#ffb089');
  const hookColor = () => cv('--flow-hook', '#eef1f6');

  const diagram = createDiagram(r.diagram, { facts, reducedMotion: reduced });
  const inspector = createInspector(r.pk, { facts, reducedMotion: reduced });

  // ───────── state ─────────
  let transport = 'usb';
  let frames = [];
  let nextFrameId = 1;
  let pinned = null; // frame id the user picked; null = follow live
  let visible = false;
  let samples = true;
  const sessions = new Map();
  const frameById = new Map();

  // ───────── transport switch ─────────
  TRANSPORTS.forEach((k) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = `fx-tsw lane-${k}`;
    b.setAttribute('role', 'radio');
    b.dataset.k = k;
    b.innerHTML = `<b>${LANE_META[k].label}</b><span>${LANE_META[k].sub}</span>`;
    b.addEventListener('click', () => setTransport(k, true));
    b.addEventListener('keydown', (e) => {
      const d = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0;
      if (!d) return;
      e.preventDefault();
      const n = TRANSPORTS[(TRANSPORTS.indexOf(transport) + d + 3) % 3];
      setTransport(n, true);
      r.tswitch.querySelector(`[data-k="${n}"]`).focus();
    });
    r.tswitch.append(b);
  });

  function setTransport(k, fromUser, force) {
    if (k === transport && !fromUser && !force) return;
    transport = k;
    root.dataset.transport = k;
    for (const b of r.tswitch.children) {
      b.setAttribute('aria-checked', String(b.dataset.k === k));
      b.tabIndex = b.dataset.k === k ? 0 : -1;
    }
    r.blurb.textContent = BLURB[k];
    r.tags.replaceChildren(...tagsFor(k, facts ?? {}).map(([a, v]) => {
      const li = document.createElement('li');
      li.innerHTML = `<span>${a}</span><code></code>`;
      li.querySelector('code').textContent = v ?? '—';
      return li;
    }));
    diagram.setTransport(k);
    inspector.setTransport(k);
    stageFocus();
    if (fromUser) bus?.emit('transport:set', { kind: k });
  }

  function stageFocus() {
    if (!visible) return;
    ctx.stage?.focus?.(transport === 'usb' ? 'usb' : transport === 'ble' ? 'antenna' : null);
  }

  // ───────── scenario controls ─────────
  const scenarios = ctx.daemon?.scenarios ?? [];
  let playing = ctx.daemon?.playing ?? true;
  let current = ctx.daemon?.scenario ?? scenarios[0]?.name;
  scenarios.forEach((s) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'fx-pill';
    b.setAttribute('role', 'radio');
    b.dataset.name = s.name;
    b.textContent = s.title;
    b.addEventListener('click', () => {
      current = s.name;
      playing = true;
      ctx.daemon?.play(s.name);
      syncControls();
    });
    r.scen.append(b);
  });
  r.play.addEventListener('click', () => {
    playing = !playing;
    if (playing) ctx.daemon?.play(); else ctx.daemon?.pause();
    syncControls();
  });
  function syncControls() {
    r.play.setAttribute('aria-pressed', String(playing));
    r.play.dataset.playing = String(playing);
    r.playTxt.textContent = playing ? 'Pause' : 'Play';
    for (const b of r.scen.children) b.setAttribute('aria-checked', String(b.dataset.name === current));
  }
  if (!scenarios.length) { r.scen.hidden = true; r.play.hidden = true; }

  // ───────── frames ─────────
  function addFrame(dir, bytes, msg, isSample = false) {
    const f = { id: nextFrameId++, dir, bytes, msg, at: Date.now(), sample: isSample };
    frames.unshift(f);
    frameById.set(f.id, f);
    if (frames.length > MAX_FRAMES) frameById.delete(frames.pop().id);
    return f;
  }

  function renderChips() {
    r.chips.replaceChildren(...frames.map((f) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = `fx-chip-btn dir-${f.dir}${(pinned ?? frames[0]?.id) === f.id ? ' is-on' : ''}`;
      b.setAttribute('role', 'option');
      b.setAttribute('aria-selected', String((pinned ?? frames[0]?.id) === f.id));
      b.title = describe(f.msg, f.dir);
      b.innerHTML = `<i aria-hidden="true">${f.dir === 'rx' ? '↓' : '↑'}</i><b></b><span></span>`;
      b.querySelector('b').textContent = f.msg.t.replace('session_upd', 'session');
      b.querySelector('span').textContent = `${f.bytes.length} B`;
      b.addEventListener('click', () => select(f.id, true));
      return b;
    }));
    r.live.hidden = pinned !== null;
  }

  function select(id, pin) {
    const f = frameById.get(id);
    if (!f) return;
    pinned = pin ? (pinned === id && frames[0]?.id !== id ? null : id) : pinned;
    const shown = pinned !== null ? frameById.get(pinned) : frames[0];
    if (shown) inspector.show(shown);
    renderChips();
    if (pin && visible) r.pk.scrollIntoView({ behavior: reduced ? 'auto' : 'smooth', block: 'nearest' });
  }

  function onFrame(dir, { bytes, msg }) {
    if (samples) { frames = []; frameById.clear(); samples = false; }
    const f = addFrame(dir, bytes, msg);
    trackSessions(dir, msg);
    if (pinned === null) inspector.show(f);
    renderChips();
    if (!visible) return;
    diagram.frame({ dir, msg, id: f.id, color: laneColor(transport), retColor: retColor() });
    if (transport !== 'ws') ctx.stage?.pulse?.(transport);
  }

  function trackSessions(dir, msg) {
    if (dir !== 'rx') return;
    let dirty = false;
    if (msg.t === 'snapshot') { sessions.clear(); (msg.b.sessions ?? []).forEach((s) => sessions.set(s.sid, { ...s })); dirty = true; }
    else if (msg.t === 'session_upd') { sessions.set(msg.b.sid, { ...(sessions.get(msg.b.sid) ?? {}), ...msg.b }); dirty = true; }
    else if (msg.t === 'gone') { sessions.delete(msg.b.sid); dirty = true; }
    if (dirty) diagram.setSessions(sessions);
  }

  // Seed the inspector with a representative exchange until real traffic arrives.
  function seed() {
    const demo = [
      ['rx', welcome(1, 1, { host: { name: 'your-laptop' } })],
      ['rx', snapshot(2, [{ sid: 1, ag: 'copilot', st: 'work', repo: 'm5-agent-link', br: 'main', tool: 'bash', t0: 1793994000, tst: 1793995100, sum: 'Running tests', seen: false, host: 0 }])],
      ['tx', cmd(5, 'focus', { sid: 1 })],
      ['tx', hello(1)],
    ];
    demo.forEach(([dir, m]) => addFrame(dir, encodeMsg(m), m, true));
    frames.reverse();
    inspector.show(frames[0]);
    renderChips();
  }

  // ───────── bus wiring ─────────
  const offs = [
    bus?.on('watch:rx', (d) => onFrame('rx', d)),
    bus?.on('watch:tx', (d) => onFrame('tx', d)),
    bus?.on('scenario:step', (d) => {
      r.now.textContent = d.text;
      if (ctx.daemon && playing !== ctx.daemon.playing) { playing = ctx.daemon.playing; }
      syncControls();
      if (visible) diagram.hookEvent(hookColor());
      if (d.name && d.name !== current) { current = d.name; syncControls(); }
    }),
    bus?.on('pc:focus', ({ sid, ok }) => {
      const s = sessions.get(sid);
      r.ret.textContent = ok ? `cmd focus → session ${sid}${s?.repo ? ` (${s.repo})` : ''}: the daemon's focus driver raised that terminal.` : `cmd focus → session ${sid}: no window found.`;
      r.ret.classList.add('is-hot');
      setTimeout(() => r.ret.classList.remove('is-hot'), 2400);
      if (visible) diagram.focusReturn({ agent: s?.ag, color: retColor() });
      else diagram.focusReturn({ agent: s?.ag, color: retColor() });
    }),
    bus?.on('transport:set', ({ kind }) => TRANSPORTS.includes(kind) && setTransport(kind, false)),
  ].filter(Boolean);

  diagram.onBeadClick((id) => select(id, true));

  // narrow screens get the vertical pipeline
  const mq = window.matchMedia('(max-width: 760px)');
  const applyLayout = () => { diagram.resize(mq.matches); diagram.setTransport(transport); diagram.setSessions(sessions); };
  mq.addEventListener?.('change', applyLayout);
  applyLayout();

  const io = new IntersectionObserver((es) => {
    const v = es.some((e) => e.isIntersecting);
    if (v === visible) return;
    visible = v;
    if (v) stageFocus(); else ctx.stage?.focus?.(null);
  }, { rootMargin: '-10% 0px -10% 0px' });
  io.observe(r.diagram);

  seed();
  setTransport('usb', false, true);
  syncControls();

  return { destroy() { offs.forEach((o) => o()); io.disconnect(); } };
}

export function onEnter() {}
export function onLeave() {}
