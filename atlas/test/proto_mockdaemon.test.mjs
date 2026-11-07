import test from 'node:test';
import assert from 'node:assert/strict';
import { createMockDaemon } from '../js/proto/mock-daemon.js';
import { encodeMsg, decodeMsg, hello, ping, cmd } from '../js/proto/messages.js';
import { encode, decode } from '../js/proto/cbor.js';

function harness(opts = {}) {
  let t = 1_793_995_200_000;
  const q = [];
  const timers = {
    setTimeout: (fn, ms) => { const h = { fn, at: t + ms, live: true }; q.push(h); return h; },
    clearTimeout: (h) => { h.live = false; },
  };
  const advance = (ms) => {
    const end = t + ms;
    for (;;) {
      const next = q.filter((h) => h.live).sort((a, b) => a.at - b.at)[0];
      if (!next || next.at > end) break;
      next.live = false;
      t = next.at;
      next.fn();
    }
    t = end;
  };
  const events = [];
  const bus = { emit: (type, detail) => events.push({ type, detail }) };
  const daemon = createMockDaemon({ bus, timers, now: () => t, ...opts });
  const rx = [];
  let txCb;
  const watch = { onTx: (f) => { txCb = f; }, deliver: (b) => rx.push(decodeMsg(b)) };
  daemon.attach(watch);
  const send = (msg) => txCb(encodeMsg(msg));
  return { daemon, events, rx, send, advance };
}

test('hello → welcome (re) + snapshot, then deltas for demo, all CDDL-decodable', () => {
  const h = harness();
  h.daemon.play('demo');
  h.advance(3000); // nothing delivered before the handshake
  assert.equal(h.rx.length, 0);
  h.send(hello(1));
  h.advance(100);
  const [welcome, snap] = h.rx;
  assert.equal(welcome.t, 'welcome');
  assert.equal(welcome.re, 1);
  assert.equal(welcome.b.host.name, 'your-laptop');
  assert.equal(welcome.b.host.id.length, 16);
  assert.equal(snap.t, 'snapshot');
  assert.deepEqual(snap.b.sessions.map((s) => s.sid), [1, 2]);
  assert.ok(snap.id > welcome.id);
  h.advance(12000);
  const kinds = h.rx.slice(2).map((m) => m.t);
  assert.ok(kinds.includes('attention'));
  const done = h.rx.find((m) => m.t === 'session_upd' && m.b.st === 'done');
  assert.ok(done && done.b.sid === 1 && done.b.tst > 0);
  assert.ok(!('repo' in done.b) && !('ag' in done.b)); // delta, not the full object
  const ids = h.rx.map((m) => m.id);
  assert.deepEqual(ids, [...ids].sort((a, b) => a - b));
  // wire-level: every frame is an integer-keyed map with t, id and a body map
  for (const ev of h.events.filter((e) => e.type === 'watch:rx')) {
    const env = decode(ev.detail.bytes);
    assert.ok(env instanceof Map && typeof env.get(0) === 'number' && typeof env.get(1) === 'number' && env.get(3) instanceof Map);
    assert.deepEqual(encode(env), ev.detail.bytes);
  }
  assert.ok(h.events.some((e) => e.type === 'watch:tx' && e.detail.msg.t === 'hello'));
  assert.ok(h.events.some((e) => e.type === 'scenario:step'));
});

test('ping, focus, stale approve, unsupported', () => {
  const h = harness();
  h.send(hello(1));
  h.daemon.play('demo');
  h.advance(3000);
  h.rx.length = 0;
  h.send(ping(2));
  h.send(cmd(3, 'focus', { sid: 2 }));
  h.send(cmd(4, 'focus', { sid: 99 }));
  h.send(cmd(5, 'approve', { sid: 2, rid: 7, hold_ms: 1250 }));
  h.send(cmd(6, 'quiet'));
  h.advance(200);
  const byRe = Object.fromEntries(h.rx.filter((m) => m.re).map((m) => [m.re, m]));
  assert.equal(byRe[2].t, 'pong');
  assert.deepEqual(byRe[3].b, { ok: true });
  assert.equal(byRe[4].b.err, 'stale');
  assert.equal(byRe[5].b.err, 'stale'); // no live request yet
  assert.equal(byRe[6].b.err, 'unsupported');
  assert.deepEqual(h.events.filter((e) => e.type === 'pc:focus').map((e) => e.detail), [{ sid: 2, ok: true }]);
});

test('attention scenario: approve after the hold clears the request', () => {
  const h = harness();
  h.send(hello(1));
  h.daemon.play('attention');
  h.advance(3500);
  const att = h.rx.find((m) => m.t === 'attention');
  assert.ok(att);
  h.send(cmd(2, 'approve', { sid: 1, rid: att.b.rid, hold_ms: 500 }));
  h.advance(100);
  assert.equal(h.rx.find((m) => m.re === 2).b.err, 'denied');
  h.send(cmd(3, 'approve', { sid: 1, rid: att.b.rid, hold_ms: 1200 }));
  h.advance(200);
  assert.equal(h.rx.find((m) => m.re === 3).b.ok, true);
  assert.ok(h.rx.some((m) => m.t === 'clear' && m.b.why === 'answered_dev'));
});

test('scenarios: busy has 8 sessions, pause/resume, loop, inject', () => {
  const h = harness();
  h.send(hello(1));
  h.advance(50);
  h.daemon.play('busy');
  h.advance(3000);
  assert.equal(h.daemon.sessions.length, 8);
  h.daemon.pause();
  const n = h.rx.length;
  h.advance(30000);
  assert.equal(h.rx.length, n);
  h.daemon.play();
  h.advance(40000);
  assert.ok(h.daemon.loops >= 1);
  h.daemon.inject({ sid: 20, ag: 'claude', st: 'work', repo: 'x' });
  h.daemon.inject({ sid: 20, st: 'done' });
  h.daemon.inject({ gone: 20 });
  h.advance(10);
  assert.deepEqual(h.rx.slice(-3).map((m) => m.t), ['session_upd', 'session_upd', 'gone']);
  assert.deepEqual(h.daemon.scenarios.map((s) => s.name), ['demo', 'single', 'attention', 'busy']);
});

test('a second hello (device reboot) resends welcome and a current snapshot', () => {
  const h = harness();
  h.send(hello(1));
  h.daemon.play('single');
  h.advance(1000);
  h.rx.length = 0;
  h.send(hello(1));
  h.advance(100);
  assert.deepEqual(h.rx.map((m) => m.t), ['welcome', 'snapshot']);
  assert.equal(h.rx[1].b.sessions.length, 1);
});
