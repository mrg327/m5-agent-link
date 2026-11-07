// The pipeline drawing: SVG threads + terminals/hook/daemon/watch nodes, with beads of light
// travelling along the threads. Pure DOM/SVG; no dependencies.
const NS = 'http://www.w3.org/2000/svg';
const el = (name, attrs = {}, parent) => {
  const n = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
  parent?.append(n);
  return n;
};
const txt = (parent, x, y, s, cls, attrs = {}) => {
  const t = el('text', { x, y, class: cls, ...attrs }, parent);
  t.textContent = s;
  return t;
};

export const LANE_META = {
  usb: { label: 'USB', sub: 'CDC serial' },
  ble: { label: 'BLE', sub: 'GATT notify/write' },
  ws: { label: 'WS', sub: 'WebSocket' },
};

function layout(vertical) {
  if (!vertical) {
    const Y = 200;
    const lane = (dy) => `M646,${Y} C720,${Y} 724,${Y + dy} 790,${Y + dy} L862,${Y + dy} C940,${Y + dy} 940,${Y} 1004,${Y}`;
    return {
      w: 1200, h: 320, y0: 56,
      term: { front: { x: 30, y: 148 }, back: { x: 70, y: 92 }, w: 180, h: 112 },
      agentPort: [210, Y], hook: { x: 340, y: Y }, daemon: { x: 548, y: Y, w: 196, h: 128 },
      watch: { x: 1092, y: Y, r: 76 },
      paths: {
        agentHook: `M212,${Y} L322,${Y}`, hookDaemon: `M358,${Y} L450,${Y}`,
        lanes: { usb: lane(-98), ble: lane(0), ws: lane(98) },
      },
      laneLabel: (k) => ({ x: 820, y: Y + ({ usb: -98, ble: 0, ws: 98 }[k]) - 14, anchor: 'middle' }),
      segLabel: { agentHook: [268, Y - 12], hookDaemon: [404, Y - 12] },
      nodeLabel: { hook: [340, Y + 58], daemon: [548, Y + 98], watch: [1092, Y + 112], agent: [120, 316] },
    };
  }
  const X = 180;
  const lane = (dx) => `M${X},362 C${X},410 ${X + dx},420 ${X + dx},458 L${X + dx},500 C${X + dx},560 ${X},560 ${X},600`;
  return {
    w: 360, h: 760, y0: 0,
    term: { front: { x: 104, y: 70 }, back: { x: 142, y: 30 }, w: 150, h: 96 },
    agentPort: [X, 168], hook: { x: X, y: 226 }, daemon: { x: X, y: 314, w: 200, h: 96 },
    watch: { x: X, y: 666, r: 66 },
    paths: {
      agentHook: `M${X},170 L${X},208`, hookDaemon: `M${X},244 L${X},266`,
      lanes: { usb: lane(-112), ble: lane(0), ws: lane(112) },
    },
    laneLabel: (k) => ({ x: X + ({ usb: -112, ble: 0, ws: 112 }[k]) + (k === 'ble' ? 12 : 0), y: 480, anchor: k === 'ble' ? 'start' : 'middle' }),
    segLabel: { agentHook: [X + 12, 192], hookDaemon: [X + 12, 260] },
    nodeLabel: { hook: [X + 34, 230], daemon: [X, 380], watch: [X, 754], agent: [X, 14] },
  };
}

const TERMS = {
  copilot: { name: 'Copilot CLI', lines: ['$ copilot', '● running tests…'] },
  claude: { name: 'Claude Code', lines: ['> claude', '✻ editing files…'] },
};

export function createDiagram(host, { facts, reducedMotion }) {
  const stateColor = (st) => facts?.states?.[st]?.color ?? '#6e7681';
  let vertical = false;
  let L;
  let svg;
  let transport = 'usb';
  const beads = new Set();
  let raf = 0;
  const clicks = new Set();
  const agentState = { copilot: 'idle', claude: 'idle' };
  let front = 'copilot';
  let sessions = new Map();
  let nodes = {};
  let laneEls = {};
  let routes = {};

  function build() {
    L = layout(vertical);
    host.replaceChildren();
    svg = el('svg', { viewBox: `0 ${L.y0} ${L.w} ${L.h}`, class: 'fx-svg', role: 'img', 'aria-label': 'Pipeline: agent terminal to hook to daemon to USB, Bluetooth or WebSocket to the watch' }, host);
    const defs = el('defs', {}, svg);
    const blur = el('filter', { id: 'fx-blur', x: '-50%', y: '-50%', width: '200%', height: '200%' }, defs);
    el('feGaussianBlur', { stdDeviation: 3.2 }, blur);
    const blur2 = el('filter', { id: 'fx-blur-lg', x: '-50%', y: '-50%', width: '200%', height: '200%' }, defs);
    el('feGaussianBlur', { stdDeviation: 9 }, blur2);
    const puck = el('radialGradient', { id: 'fx-puck', cx: '38%', cy: '30%', r: '85%' }, defs);
    el('stop', { offset: 0, 'stop-color': '#2a2e38' }, puck);
    el('stop', { offset: 0.55, 'stop-color': '#0d0f15' }, puck);
    el('stop', { offset: 1, 'stop-color': '#030406' }, puck);
    const term = el('linearGradient', { id: 'fx-term', x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
    el('stop', { offset: 0, 'stop-color': '#12151d' }, term);
    el('stop', { offset: 1, 'stop-color': '#07080c' }, term);

    // threads (drawn first so nodes sit on top)
    const threads = el('g', { class: 'fx-threads' }, svg);
    const thread = (key, d, cls = '') => {
      const g = el('g', { class: `fx-thread ${cls}`, 'data-thread': key }, threads);
      el('path', { d, class: 'fx-thread__glow' }, g);
      if (cls.includes('is-lane')) {
        for (const o of [-2.6, 2.6]) el('path', { d, class: 'fx-thread__strand', transform: vertical ? `translate(${o} 0)` : `translate(0 ${o})` }, g);
      }
      const core = el('path', { d, class: 'fx-thread__core' }, g);
      return { g, core };
    };
    const ah = thread('agentHook', L.paths.agentHook, 'is-hook');
    const hd = thread('hookDaemon', L.paths.hookDaemon, 'is-hook');
    laneEls = {};
    for (const k of ['usb', 'ble', 'ws']) {
      laneEls[k] = thread(k, L.paths.lanes[k], `is-lane lane-${k}`);
      const p = L.laneLabel(k);
      const t = txt(threads, p.x, p.y, LANE_META[k].label, `fx-lane-label lane-${k}`, { 'text-anchor': p.anchor, 'data-lane': k });
      laneEls[k].label = t;
    }
    txt(threads, ...L.segLabel.agentHook, 'hook event', 'fx-seg', { 'text-anchor': vertical ? 'start' : 'middle' });
    txt(threads, ...L.segLabel.hookDaemon, 'unix socket', 'fx-seg', { 'text-anchor': vertical ? 'start' : 'middle' });
    routes = {
      agentHook: ah.core, hookDaemon: hd.core,
      usb: laneEls.usb.core, ble: laneEls.ble.core, ws: laneEls.ws.core,
    };

    nodes = {};
    buildTerminals();
    buildHook();
    buildDaemon();
    buildWatch();
    nodes.beads = el('g', { class: 'fx-beads' }, svg);
    applyTransport();
    applyFront(true);
  }

  function buildTerminals() {
    const g = el('g', { class: 'fx-node fx-agent', 'data-node': 'agent' }, svg);
    nodes.agent = g;
    nodes.term = {};
    for (const key of ['claude', 'copilot']) {
      const t = el('g', { class: 'fx-term', 'data-agent': key }, g);
      const { w, h } = L.term;
      el('rect', { x: 0, y: 0, width: w, height: h, rx: 10, class: 'fx-term__body' }, t);
      el('path', { d: `M0,22 H${w}`, class: 'fx-term__rule' }, t);
      [0, 1, 2].forEach((i) => el('circle', { cx: 14 + i * 13, cy: 11, r: 3.2, class: 'fx-term__dot' }, t));
      txt(t, w - 12, 15, TERMS[key].name, 'fx-term__title', { 'text-anchor': 'end' });
      TERMS[key].lines.forEach((l, i) => txt(t, 14, 48 + i * 20, l, 'fx-term__line'));
      el('rect', { x: 14, y: 76, width: 8, height: 13, class: 'fx-term__cursor' }, t);
      el('circle', { cx: w - 14, cy: h - 14, r: 4, class: 'fx-term__state' }, t);
      el('rect', { x: -2, y: -2, width: w + 4, height: h + 4, rx: 12, class: 'fx-term__ring' }, t);
      nodes.term[key] = t;
    }
    const [lx, ly] = L.nodeLabel.agent;
    txt(g, lx, ly, 'Your terminal', 'fx-node__name', { 'text-anchor': vertical ? 'middle' : 'middle' });
    if (!vertical) txt(g, lx, ly + 18, 'Copilot CLI · Claude Code', 'fx-node__sub', { 'text-anchor': 'middle' });
  }

  function buildHook() {
    const { x, y } = L.hook;
    const g = el('g', { class: 'fx-node fx-hook', 'data-node': 'hook', transform: `translate(${x} ${y})` }, svg);
    nodes.hook = g;
    el('circle', { r: 20, class: 'fx-hook__halo', filter: 'url(#fx-blur)' }, g);
    // a four-point spark
    el('path', { d: 'M0,-17 C2,-5 5,-2 17,0 C5,2 2,5 0,17 C-2,5 -5,2 -17,0 C-5,-2 -2,-5 0,-17Z', class: 'fx-hook__spark' }, g);
    const [lx, ly] = L.nodeLabel.hook;
    txt(g, lx - x, ly - y, 'agentlink-hook', 'fx-node__name', { 'text-anchor': vertical ? 'start' : 'middle' });
    txt(g, lx - x, ly - y + 17, 'tiny, one per event', 'fx-node__sub', { 'text-anchor': vertical ? 'start' : 'middle' });
  }

  function buildDaemon() {
    const { x, y, w, h } = L.daemon;
    const g = el('g', { class: 'fx-node fx-daemon', 'data-node': 'daemon', transform: `translate(${x - w / 2} ${y - h / 2})` }, svg);
    nodes.daemon = g;
    el('rect', { width: w, height: h, rx: 14, class: 'fx-daemon__body' }, g);
    txt(g, w / 2, 30, 'agentlinkd', 'fx-daemon__name', { 'text-anchor': 'middle' });
    [['registry', 0], ['focus drivers', 1]].forEach(([s, i]) => {
      const cw = i ? 96 : 70;
      const cx = i ? w - 12 - cw : 12;
      el('rect', { x: cx, y: h - 56, width: cw, height: 26, rx: 13, class: 'fx-chip' }, g);
      txt(g, cx + cw / 2, h - 38.5, s, 'fx-chip__t', { 'text-anchor': 'middle' });
    });
    el('path', { d: `M14,44 H${w - 14}`, class: 'fx-daemon__rule' }, g);
    const [lx, ly] = L.nodeLabel.daemon;
    txt(g, lx - (x - w / 2), ly - (y - h / 2), 'the host-side hub', 'fx-node__sub', { 'text-anchor': 'middle' });
  }

  function buildWatch() {
    const { x, y, r } = L.watch;
    const g = el('g', { class: 'fx-node fx-watch', 'data-node': 'watch', transform: `translate(${x} ${y})` }, svg);
    nodes.watch = g;
    el('circle', { r: r + 14, class: 'fx-watch__halo', filter: 'url(#fx-blur-lg)' }, g);
    // buttons
    el('rect', { x: -r - 5, y: 6, width: 10, height: 24, rx: 5, class: 'fx-watch__btn fx-watch__btn--a', transform: 'rotate(-32 ' + (-r) + ' 18)' }, g);
    el('rect', { x: r - 5, y: 6, width: 10, height: 24, rx: 5, class: 'fx-watch__btn fx-watch__btn--b', transform: 'rotate(32 ' + r + ' 18)' }, g);
    el('circle', { r, class: 'fx-watch__puck' }, g);
    el('circle', { r: r - 7, class: 'fx-watch__screen' }, g);
    nodes.watchRing = el('g', { class: 'fx-watch__ring' }, g);
    nodes.watchGlyph = txt(g, 0, 6, '', 'fx-watch__glyph', { 'text-anchor': 'middle' });
    const [lx, ly] = L.nodeLabel.watch;
    txt(g, lx - x, ly - y, 'the watch', 'fx-node__name', { 'text-anchor': 'middle' });
    if (!vertical) txt(g, lx - x, ly - y + 17, 'M5Stack StopWatch', 'fx-node__sub', { 'text-anchor': 'middle' });
    drawRing();
  }

  function drawRing() {
    if (!nodes.watchRing) return;
    nodes.watchRing.replaceChildren();
    const list = [...sessions.values()].slice(0, 8);
    const R = L.watch.r - 20;
    nodes.watchGlyph.textContent = list.length ? '' : '·';
    list.forEach((s, i) => {
      const a = -Math.PI / 2 + (i / Math.max(list.length, 1)) * Math.PI * 2;
      const dot = el('circle', { cx: Math.cos(a) * R * (list.length === 1 ? 0 : 1), cy: Math.sin(a) * R * (list.length === 1 ? 0 : 1), r: list.length > 5 ? 5 : 7, class: 'fx-watch__s' }, nodes.watchRing);
      dot.style.fill = stateColor(s.st);
      dot.style.filter = `drop-shadow(0 0 5px ${stateColor(s.st)})`;
    });
  }

  function applyTransport() {
    for (const [k, l] of Object.entries(laneEls)) l.g.classList.toggle('is-on', k === transport);
    svg.dataset.transport = transport;
  }

  function applyFront(instant) {
    const { front: f, back: b, w } = L.term;
    const other = front === 'copilot' ? 'claude' : 'copilot';
    const place = (key, p, scale, z) => {
      const t = nodes.term[key];
      t.style.transition = instant || reducedMotion ? 'none' : '';
      t.style.transform = `translate(${p.x}px, ${p.y}px) scale(${scale})`;
      t.classList.toggle('is-front', z);
      t.style.transformOrigin = '0 0';
    };
    // draw the front one last so it overlaps
    nodes.agent.append(nodes.term[other]);
    nodes.agent.append(nodes.term[front]);
    place(other, b, 0.9, false);
    place(front, f, 1, true);
    for (const k of Object.keys(TERMS)) nodes.term[k].querySelector('.fx-term__state').style.fill = stateColor(agentState[k]);
    void w;
  }

  // ───────── beads ─────────
  function routeOf(segs) {
    const parts = segs.map(({ key, rev }) => ({ p: routes[key], len: routes[key].getTotalLength(), rev }));
    return { parts, total: parts.reduce((a, s) => a + s.len, 0) };
  }
  function pointAt(route, s) {
    let rest = Math.max(0, Math.min(route.total, s));
    for (const part of route.parts) {
      if (rest <= part.len || part === route.parts.at(-1)) {
        const pt = part.p.getPointAtLength(part.rev ? part.len - rest : rest);
        return pt;
      }
      rest -= part.len;
    }
    return { x: 0, y: 0 };
  }
  const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - ((-2 * t + 2) ** 3) / 2);

  /** Sends a bead along a route. opts: {segs, color, ms, size, frameId, title, delay, onDone} */
  function bead(opts) {
    const route = routeOf(opts.segs);
    const g = el('g', { class: 'fx-bead' + (opts.faint ? ' is-faint' : ''), tabindex: opts.frameId ? 0 : -1 }, nodes.beads);
    if (opts.title) el('title', {}, g).textContent = opts.title;
    const hit = el('circle', { r: 16, class: 'fx-bead__hit' }, g);
    const ghosts = [];
    for (let i = 5; i >= 0; i--) {
      const c = el('circle', { r: (opts.size ?? 5) * (1 - i * 0.12), class: 'fx-bead__ghost' }, g);
      c.style.fill = opts.color;
      c.style.opacity = (1 - i / 6) * (i === 0 ? 1 : 0.55);
      ghosts.push(c);
    }
    const halo = el('circle', { r: (opts.size ?? 5) * 2.6, class: 'fx-bead__halo' }, g);
    halo.style.fill = opts.color;
    g.insertBefore(halo, g.firstChild.nextSibling);
    if (opts.frameId) {
      const go = () => clicks.forEach((f) => f(opts.frameId));
      g.addEventListener('click', go);
      g.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); go(); } });
    }
    const b = { route, g, ghosts, halo, hit, born: performance.now() + (opts.delay ?? 0), ms: opts.ms ?? 900, onDone: opts.onDone };
    g.style.opacity = 0;
    beads.add(b);
    if (!raf) raf = requestAnimationFrame(tick);
    return b;
  }

  function tick(now) {
    for (const b of [...beads]) {
      const k = (now - b.born) / b.ms;
      if (k < 0) continue;
      if (k >= 1) {
        b.g.remove();
        beads.delete(b);
        b.onDone?.();
        continue;
      }
      b.g.style.opacity = Math.min(1, k * 8, (1 - k) * 8 + 0.2);
      b.ghosts.forEach((c, i) => {
        const back = (5 - i) * 0.03;
        const p = pointAt(b.route, ease(Math.max(0, k - back)) * b.route.total);
        c.setAttribute('cx', p.x);
        c.setAttribute('cy', p.y);
        if (i === 5) {
          b.halo.setAttribute('cx', p.x);
          b.halo.setAttribute('cy', p.y);
          b.hit.setAttribute('cx', p.x);
          b.hit.setAttribute('cy', p.y);
        }
      });
    }
    raf = beads.size ? requestAnimationFrame(tick) : 0;
  }

  function hit(nodeKey) {
    const n = nodes[nodeKey];
    if (!n) return;
    n.classList.remove('is-hit');
    void n.getBoundingClientRect();
    n.classList.add('is-hit');
    setTimeout(() => n.classList.remove('is-hit'), 650);
  }

  // ───────── public ─────────
  const api = {
    el: host,
    onBeadClick: (f) => { clicks.add(f); return () => clicks.delete(f); },
    setTransport(k) { transport = k; applyTransport(); },
    setSessions(map) { sessions = map; for (const s of map.values()) if (s.ag in agentState && s.st) agentState[s.ag] = s.st; drawRing(); applyFront(true); },

    /** A decoded frame crossed the link. dir 'rx' = PC→watch, 'tx' = watch→PC. */
    frame({ dir, msg, id, color, retColor, viaAgent }) {
      const small = msg.t === 'ping' || msg.t === 'pong';
      const lane = { key: transport, rev: dir === 'tx' };
      if (reducedMotion) {
        hit(dir === 'rx' ? 'watch' : 'daemon');
        laneEls[transport].g.classList.add('is-hit');
        setTimeout(() => laneEls[transport].g.classList.remove('is-hit'), 500);
        return;
      }
      const title = `${dir === 'rx' ? '↓' : '↑'} ${msg.t}`;
      if (dir === 'rx') {
        bead({ segs: [lane], color, ms: 1000, size: small ? 3.6 : 6, frameId: id, title, faint: small, onDone: () => hit('watch') });
      } else {
        bead({ segs: [lane], color: retColor, ms: 1000, size: small ? 3.6 : 6, frameId: id, title, faint: small, onDone: () => hit('daemon') });
      }
      void viaAgent;
    },

    /** The agent's hook fired: a spark runs agent → hook → daemon. */
    hookEvent(color) {
      if (reducedMotion) return hit('hook');
      hit('agent');
      bead({ segs: [{ key: 'agentHook' }, { key: 'hookDaemon' }], color, ms: 700, size: 3.6, title: 'hook event', onDone: () => hit('daemon') });
      setTimeout(() => hit('hook'), 280);
    },

    /** cmd focus was honoured: a bead runs back to the terminal, which then comes to the front. */
    focusReturn({ agent, color }) {
      const arrive = () => {
        if (agent && agent in TERMS) front = agent;
        applyFront(false);
        const t = nodes.term[front];
        t.classList.remove('is-pop');
        void t.getBoundingClientRect();
        t.classList.add('is-pop');
        setTimeout(() => t.classList.remove('is-pop'), 1400);
      };
      if (reducedMotion) return arrive();
      hit('daemon');
      bead({ segs: [{ key: 'hookDaemon', rev: true }, { key: 'agentHook', rev: true }], color, ms: 800, size: 6, title: 'pc:focus', onDone: arrive });
    },

    resize(isVertical) {
      if (isVertical === vertical && svg) return;
      vertical = isVertical;
      beads.clear();
      build();
    },
    nodeEl: (k) => nodes[k],
  };
  api.resize(false);
  return api;
}
