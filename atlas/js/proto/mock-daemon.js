// JS stand-in for the daemon side of the AgentLink protocol. It speaks real CBOR envelopes
// (see messages.js) so the real firmware core can be driven by it. Semantics follow
// daemon/crates/agentlinkd/src/{registry,device}.rs: hello → welcome(re=hello.id) + snapshot,
// session deltas carry only changed keys (+tst on state change), ping → pong, cmd focus →
// result (unknown sid = stale), approve/deny with no live request = stale.
import { encodeMsg, decodeMsg } from './messages.js';

const MIN_APPROVE_HOLD_MS = 1000; // device.rs MIN_APPROVE_HOLD_MS
const clip = (s, n) => (s.length <= n ? s : s.slice(0, n - 1) + '…');

const S = (sid, ag, repo, br, st, extra = {}) => ({ sid, ag, repo, br, st, seen: false, host: 0, ...extra });

// Each step: [atMs, text, (d) => void]. A scenario loops after `length` ms.
const SCENARIOS = {
  demo: {
    title: 'Three agents, one watch',
    length: 34000,
    steps: [
      [0, 'Copilot CLI starts working on m5-agent-link', (d) => d.addSession(S(1, 'copilot', 'm5-agent-link', 'main', 'work', { tool: 'bash', sum: 'Running tests' }))],
      [2500, 'Claude Code joins in webapp', (d) => d.addSession(S(2, 'claude', 'webapp', 'feat/login', 'work', { tool: 'Edit', sum: 'Editing login form' }))],
      [5000, 'A second Copilot session boots up', (d) => d.addSession(S(3, 'copilot', 'docs-site', 'main', 'start'))],
      [6500, 'docs-site starts working', (d) => d.update(3, { st: 'work', tool: 'grep', sum: 'Searching docs' })],
      [9000, 'Claude Code needs permission to run a command', (d) => d.requestApproval(2, { rid: 7, title: 'Run shell command', text: 'npm test -- --watch=false' })],
      [12500, 'Copilot finishes its tests', (d) => d.update(1, { st: 'done', tool: undefined, sum: 'Tests passed' })],
      [15500, 'The request is answered in the terminal', (d) => d.clearAttention(7, 'answered_pc')],
      [18000, 'docs-site has a question for you', (d) => d.update(3, { st: 'input', sub: 'question', sum: 'Which version to document?' })],
      [21500, 'You answer; docs-site carries on', (d) => d.update(3, { st: 'work', tool: 'Edit', sum: 'Writing docs' })],
      [24000, 'Claude Code is done', (d) => d.update(2, { st: 'done', tool: undefined, sum: 'Login form refactored' })],
      [27000, 'docs-site is done', (d) => d.update(3, { st: 'done', tool: undefined, sum: 'Docs updated' })],
      [31000, 'Sessions wind down', (d) => { d.removeSession(1); d.removeSession(2); d.removeSession(3); }],
    ],
  },
  single: {
    title: 'One agent, quietly working',
    length: 24000,
    steps: [
      [0, 'Copilot CLI starts', (d) => d.addSession(S(1, 'copilot', 'm5-agent-link', 'main', 'work', { tool: 'view', sum: 'Reading the code' }))],
      [4000, 'Running a command', (d) => d.update(1, { tool: 'bash', sum: 'Running tests' })],
      [9000, 'Editing a file', (d) => d.update(1, { tool: 'edit', sum: 'Fixing a bug' })],
      [15000, 'Done', (d) => d.update(1, { st: 'done', tool: undefined, sum: 'All green' })],
      [21000, 'Session ends', (d) => d.removeSession(1)],
    ],
  },
  attention: {
    title: 'A permission request',
    length: 26000,
    steps: [
      [0, 'Claude Code starts', (d) => d.addSession(S(1, 'claude', 'webapp', 'feat/login', 'work', { tool: 'Edit', sum: 'Editing login form' }))],
      [600, 'Copilot CLI is busy in api', (d) => d.addSession(S(2, 'copilot', 'api', 'main', 'work', { tool: 'bash', sum: 'Running migrations' }))],
      [1200, 'docs-site finished earlier', (d) => d.addSession(S(3, 'copilot', 'docs-site', 'main', 'done', { sum: 'Docs updated' }))],
      [1800, 'infra has a question', (d) => d.addSession(S(4, 'claude', 'infra', 'main', 'input', { sub: 'question', sum: 'Which region?' }))],
      [3000, 'Claude Code asks to run a shell command', (d) => d.requestApproval(1, { rid: 7, title: 'Run shell command', text: 'npm test -- --watch=false', exp: 20 })],
      [20000, 'Nobody answered, so the request expires', (d) => d.expireAttention(7)],
      [22000, 'Claude Code carries on', (d) => d.update(1, { st: 'done', sum: 'Stopped waiting' })],
    ],
  },
  ring: {
    title: 'One session in every state',
    length: 3600000,
    steps: [
      [0, 'Working', (d) => d.addSession(S(1, 'copilot', 'm5-agent-link', 'main', 'work', { tool: 'bash', sum: 'Running tests' }))],
      [0, 'Has a question', (d) => d.addSession(S(2, 'claude', 'webapp', 'feat/login', 'input', { sub: 'question', sum: 'Which version to document?' }))],
      [0, 'Needs permission', (d) => d.addSession(S(3, 'claude', 'api', 'main', 'work', { tool: 'bash' }))],
      [0, 'Done', (d) => d.addSession(S(4, 'copilot', 'docs-site', 'main', 'done', { sum: 'Docs updated' }))],
      [0, 'Error', (d) => d.addSession(S(5, 'copilot', 'infra', 'main', 'error', { sum: 'Build failed' }))],
      [0, 'Idle', (d) => d.addSession(S(6, 'claude', 'blog', 'main', 'idle', { sum: 'Waiting for a prompt' }))],
      [100, 'Permission request', (d) => d.requestApproval(3, { rid: 7, title: 'Run shell command', text: 'npm test -- --watch=false', exp: 3000 })],
    ],
  },
  busy: {
    title: 'Eight sessions at once',
    length: 24000,
    steps: [
      ...Array.from({ length: 8 }, (_, i) => [i * 350, `Session ${i + 1} starts`, (d) => d.addSession(S(i + 1, i % 2 ? 'claude' : 'copilot', ['api', 'webapp', 'docs', 'infra', 'mobile', 'cli', 'ml-train', 'blog'][i], i % 3 ? 'main' : 'feat/x', 'work', { tool: ['bash', 'Edit', 'grep', 'view'][i % 4], sum: 'Working' }))]),
      [4000, 'Two agents need you', (d) => { d.update(2, { st: 'input', sub: 'approval' }); d.update(5, { st: 'input', sub: 'question', sum: 'Pick a name?' }); }],
      [8000, 'Three finish', (d) => { for (const s of [1, 3, 6]) d.update(s, { st: 'done', tool: undefined, sum: 'Finished' }); }],
      [12000, 'An error appears', (d) => d.update(4, { st: 'error', sum: 'Build failed' })],
      [16000, 'Back to work', (d) => { d.update(2, { st: 'work' }); d.update(5, { st: 'work' }); d.update(4, { st: 'work', sum: 'Retrying' }); }],
      [20000, 'Everything settles', (d) => { for (let s = 1; s <= 8; s++) d.update(s, { st: 'done', tool: undefined }); }],
    ],
  },
};

export function createMockDaemon({ bus, facts, timers, now, hostName = 'your-laptop', latencyMs = 25, staleApprovals = false } = {}) {
  const T = timers ?? { setTimeout: (f, ms) => globalThis.setTimeout(f, ms), clearTimeout: (h) => globalThis.clearTimeout(h) };
  const clock = now ?? (() => Date.now());
  const unix = () => Math.floor(clock() / 1000);
  const emit = (type, detail) => bus?.emit(type, detail);

  let watch = null;
  let nextId = 1; // daemon's monotonic per-link counter
  let handshaken = false;
  let sessions = new Map(); // sid → session object (symbolic)
  const live = new Map(); // rid → {sid, title, ...}
  let scenario = null;
  let cursor = 0; // next step index
  let offset = 0; // ms into the loop when last started
  let startedAt = 0;
  let handle = null;
  let playing = false;
  let loops = 0;

  const self = {
    host: { id: 'a1a2a3a4a5a6a7a8', name: hostName, os: 'mac' },
    get scenarios() { return Object.entries(SCENARIOS).map(([name, s]) => ({ name, title: s.title })); },
    get sessions() { return [...sessions.values()]; },
    get playing() { return playing; },
    get scenario() { return scenario; },
    get handshaken() { return handshaken; },
    get loops() { return loops; },

    attach(w) {
      watch = w;
      handshaken = false;
      w.onTx?.((bytes) => self.receive(bytes));
    },

    /** Raw frame from the watch. */
    receive(bytes) {
      let msg;
      try { msg = decodeMsg(bytes); } catch { return; }
      emit('watch:tx', { bytes, msg });
      T.setTimeout(() => handle_(msg), latencyMs);
    },

    send(msg) {
      msg.id ??= nextId++;
      const bytes = encodeMsg(msg);
      emit('watch:rx', { bytes, msg });
      watch?.deliver?.(bytes);
      return msg;
    },

    addSession(s) {
      const full = { ...s, t0: s.t0 ?? unix(), tst: s.tst ?? unix() };
      sessions.set(full.sid, full);
      if (handshaken) self.send({ t: 'session_upd', b: full });
    },

    /** Applies a delta (only changed keys are sent; tst is bumped on a state change). */
    update(sid, patch) {
      const s = sessions.get(sid);
      if (!s) return;
      const delta = { sid };
      for (const [k, v] of Object.entries(patch)) {
        const val = typeof v === 'string' ? clip(v, k === 'sum' ? 80 : 32) : v;
        if (val === undefined) { if (s[k] !== undefined) { delete s[k]; } continue; }
        if (s[k] !== val) { s[k] = val; delta[k] = val; }
      }
      if (patch.st !== undefined && delta.st !== undefined) {
        s.tst = delta.tst = unix();
        if (patch.sub === undefined) delete s.sub;
      }
      if (Object.keys(delta).length > 1 && handshaken) self.send({ t: 'session_upd', b: delta });
    },

    removeSession(sid) {
      if (!sessions.delete(sid)) return;
      if (handshaken) self.send({ t: 'gone', b: { sid } });
    },

    requestApproval(sid, { rid, title, text, exp = 60 }) {
      const s = sessions.get(sid);
      if (!s) return;
      self.update(sid, { st: 'input', sub: 'approval' });
      const b = { rid, sid, kind: 'approval', title, text, acts: ['approve', 'deny', 'focus'], exp: unix() + exp };
      live.set(rid, { sid, msg: b });
      if (handshaken) self.send({ t: 'attention', b });
    },

    clearAttention(rid, why = 'answered_pc') {
      const a = live.get(rid);
      if (!a) return;
      live.delete(rid);
      if (handshaken) self.send({ t: 'clear', b: { rid, why } });
      self.update(a.sid, { st: 'work' });
    },

    expireAttention(rid) { self.clearAttention(rid, 'expired'); },

    /** For replay: a symbolic session delta, or {gone: sid}, {attention: {...}}, {clear: {rid, why}}. */
    inject(patch) {
      if (patch.gone !== undefined) return self.removeSession(patch.gone);
      if (patch.attention) {
        const a = patch.attention;
        live.set(a.rid, { sid: a.sid, msg: a });
        if (handshaken) self.send({ t: 'attention', b: a });
        return;
      }
      if (patch.clear) {
        live.delete(patch.clear.rid);
        if (handshaken) self.send({ t: 'clear', b: patch.clear });
        return;
      }
      if (!sessions.has(patch.sid)) self.addSession({ host: 0, seen: false, ...patch });
      else { const { sid, ...rest } = patch; self.update(sid, rest); }
    },

    /** Clears sessions without notifying (a fresh snapshot is sent when attached and handshaken). */
    reset() {
      sessions = new Map();
      live.clear();
      if (handshaken) self.send({ t: 'snapshot', b: { sessions: [] } });
    },

    play(name) {
      if (name && SCENARIOS[name]) {
        self.stop();
        scenario = name;
        cursor = 0;
        offset = 0;
        loops = 0;
        self.reset();
      } else if (!scenario) {
        return self.play('demo');
      }
      if (playing) return;
      playing = true;
      startedAt = clock();
      schedule();
    },

    pause() {
      if (!playing) return;
      playing = false;
      offset += clock() - startedAt;
      if (handle !== null) T.clearTimeout(handle);
      handle = null;
    },

    stop() {
      self.pause();
      offset = 0;
      cursor = 0;
    },
  };

  function schedule() {
    const sc = SCENARIOS[scenario];
    if (!playing || !sc) return;
    if (cursor >= sc.steps.length) {
      // Loop: wait out the remainder of the timeline, then begin again.
      const wait = Math.max(0, sc.length - offset - (clock() - startedAt));
      handle = T.setTimeout(() => {
        cursor = 0;
        offset = 0;
        startedAt = clock();
        loops++;
        schedule();
      }, wait);
      return;
    }
    const [at] = sc.steps[cursor];
    const wait = Math.max(0, at - offset - (clock() - startedAt));
    handle = T.setTimeout(() => {
      const [, text, run] = sc.steps[cursor];
      const step = cursor++;
      emit('scenario:step', { name: scenario, step, text });
      run(self);
      schedule();
    }, wait);
  }

  function handle_(msg) {
    const b = msg.b;
    switch (msg.t) {
      case 'hello': {
        handshaken = true;
        nextId = 1;
        self.send({
          t: 'welcome', re: msg.id,
          b: { proto: [0, 1], host: self.host, time: { unix: unix(), tz_off: -new Date(clock()).getTimezoneOffset() }, cfg: { privacy: 'preview', alert: 'full' } },
        });
        self.send({ t: 'snapshot', b: { sessions: [...sessions.values()] } });
        for (const a of live.values()) {
          if (sessions.has(a.sid)) self.send({ t: 'attention', b: a.msg });
        }
        break;
      }
      case 'ping': self.send({ t: 'pong', re: msg.id, b: {} }); break;
      case 'cmd': self.send({ t: 'result', re: msg.id, b: cmdResult(b) }); break;
      default: break;
    }
  }

  function cmdResult(c) {
    const fail = (err, msg) => ({ ok: false, err, msg });
    switch (c.op) {
      case 'focus':
        if (!sessions.has(c.sid)) return fail('stale', 'unknown session');
        emit('pc:focus', { sid: c.sid, ok: true });
        return { ok: true };
      case 'approve':
      case 'deny': {
        if (c.op === 'approve' && (c.hold_ms ?? 0) < MIN_APPROVE_HOLD_MS) return fail('denied', 'hold-to-confirm too short');
        const a = live.get(c.rid);
        if (staleApprovals || !a) return fail('stale', 'no pending request for rid');
        T.setTimeout(() => self.clearAttention(c.rid, 'answered_dev'), latencyMs);
        return { ok: true };
      }
      case 'ack': return { ok: true };
      default: return fail('unsupported', 'command not implemented');
    }
  }

  return self;
}

export const scenarioNames = Object.keys(SCENARIOS);
