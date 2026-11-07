import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseTrace, buildTimeline, stateAt, basename } from '../js/replay/trace.js';

const sample = readFileSync(new URL('../data/sample-trace.jsonl', import.meta.url), 'utf8');

test('basename strips directories (privacy)', () => {
  assert.equal(basename('/Users/alice/projects/api'), 'api');
  assert.equal(basename('C:\\work\\site\\'), 'site');
});

test('sample trace parses into four sessions with ordered state steps', () => {
  const t = parseTrace(sample);
  assert.equal(t.bad, 0);
  assert.equal(t.sessions.length, 4);
  for (const s of t.sessions) {
    assert.ok(s.repo && !s.repo.includes('/'));
    const ts = s.steps.map((x) => x.t);
    assert.deepEqual(ts, [...ts].sort((a, b) => a - b));
  }
  assert.equal(t.focus.length, 4);
  assert.ok(t.duration > 600_000);
});

test('the sample contains no absolute paths or user names', () => {
  assert.ok(!/\/Users\/|\/home\/|C:\\/.test(sample));
});

test('stateAt follows transitions and returns null after gone', () => {
  const t = parseTrace(sample);
  const s1 = t.sessions.find((s) => s.sid === 1);
  assert.equal(stateAt(s1, s1.steps[0].t - 1), null);
  assert.equal(stateAt(s1, s1.steps[1].t + 1).st, s1.steps[1].st);
  assert.equal(stateAt(s1, s1.endedAt + 1), null);
});

test('timeline is sorted and tolerant of junk lines', () => {
  const t = parseTrace('not json\n{"x":1}\n' + sample);
  assert.equal(t.bad, 2);
  const ev = buildTimeline(t);
  assert.ok(ev.length > 30);
  assert.deepEqual(ev.map((e) => e.t), ev.map((e) => e.t).sort((a, b) => a - b));
  assert.ok(ev.some((e) => e.type === 'focus') && ev.some((e) => e.type === 'gone'));
});

test('falls back to usb session_upd when there are no registry lines, and never keeps cwd paths', () => {
  const line = JSON.stringify({ dir: 'out', kind: 'session_upd', src: 'usb', ts_us: 1000,
    msg: { b: { sid: 7, st: 'work', ag: 'claude', repo: '/Users/x/secret/proj' }, t: 'session_upd' } });
  const t = parseTrace(line);
  assert.equal(t.sessions[0].repo, 'proj');
  assert.equal(t.sessions[0].steps[0].patch.repo, 'proj');
});
