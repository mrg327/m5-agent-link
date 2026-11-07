import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createClassifier, createDeferrer } from '../js/gestures/classify.js';

const th = JSON.parse(readFileSync(new URL('../data/facts.json', import.meta.url), 'utf8')).gesture;

function rig() {
  const ev = [];
  const c = createClassifier((e) => ev.push(e), th);
  const kinds = () => ev.map((e) => e.kind);
  return { c, ev, kinds };
}

test('a short still touch is a tap (press, release, tap)', () => {
  const { c, kinds } = rig();
  c.touch(true, 100, 100, 0);
  c.touch(true, 105, 103, 50);
  c.touch(false, 105, 103, 90);
  assert.deepEqual(kinds(), ['press', 'release', 'tap']);
});

test('moving more than the slop makes it a swipe, in the dominant direction', () => {
  const { c, ev } = rig();
  c.touch(true, 200, 300, 0);
  c.touch(true, 200, 250, 60);
  c.touch(true, 200, 200, 100); // the release carries no position: the last move sample counts
  c.touch(false, 200, 200, 120);
  assert.equal(ev.at(-1).kind, 'swipe_u');
  assert.equal(ev.at(-1).x, 200);
  assert.equal(ev.at(-1).y, 300);
});

test('slop is strict: exactly tap_slop_px still counts as a tap', () => {
  const { c, kinds } = rig();
  c.touch(true, 100, 100, 0);
  c.touch(true, 100 + th.tap_slop_px, 100, 20);
  c.touch(false, 100 + th.tap_slop_px, 100, 40);
  assert.equal(kinds().at(-1), 'tap');
});

test('a slow or short drag emits no gesture, and keeps a pending tap', () => {
  const { c, kinds } = rig();
  c.touch(true, 100, 100, 0);
  c.touch(false, 100, 100, 50); // tap
  c.touch(true, 100, 200, 100);
  c.touch(true, 100, 230, 150); // moved, but under swipe_min_px
  c.touch(false, 100, 230, 200);
  assert.deepEqual(kinds(), ['press', 'release', 'tap', 'press', 'release']);
  c.touch(true, 300, 100, 1000);
  c.touch(true, 300, 200, 1100);
  c.touch(false, 300, 300, 1000 + th.swipe_max_ms + 1);
  assert.equal(kinds().at(-1), 'release'); // too slow
});

test('two taps inside the window, close together, give tap then double_tap (no second tap)', () => {
  const { c, kinds } = rig();
  c.touch(true, 230, 230, 0);
  c.touch(false, 230, 230, 60);
  c.touch(true, 235, 232, 60 + th.double_tap_ms);
  c.touch(false, 235, 232, 60 + th.double_tap_ms + 50);
  assert.deepEqual(kinds(), ['press', 'release', 'tap', 'press', 'release', 'double_tap']);
});

test('second tap after the window is another tap; a far second tap too', () => {
  let r = rig();
  r.c.touch(true, 230, 230, 0); r.c.touch(false, 230, 230, 60);
  r.c.touch(true, 230, 230, 61 + th.double_tap_ms); r.c.touch(false, 230, 230, 400);
  assert.equal(r.kinds().at(-1), 'tap');
  r = rig();
  r.c.touch(true, 100, 230, 0); r.c.touch(false, 100, 230, 60);
  r.c.touch(true, 100 + th.second_tap_radius_px + 1, 230, 120); r.c.touch(false, 100, 230, 160);
  assert.equal(r.kinds().at(-1), 'tap');
});

test('double-tap window is measured from the first release to the second press', () => {
  const { c, kinds } = rig();
  c.touch(true, 50, 50, 0); c.touch(false, 50, 50, 200); // long-ish first tap
  c.touch(true, 50, 50, 200 + th.double_tap_ms); c.touch(false, 50, 50, 700);
  assert.equal(kinds().at(-1), 'double_tap');
});

test('long press fires from tick while held and suppresses tap on release', () => {
  const { c, ev, kinds } = rig();
  c.touch(true, 10, 10, 0);
  c.tick(th.long_press_ms - 1);
  assert.deepEqual(kinds(), ['press']);
  c.tick(th.long_press_ms);
  c.tick(th.long_press_ms + 50);
  c.touch(false, 10, 10, 900);
  assert.deepEqual(kinds(), ['press', 'long_press', 'release']);
  assert.equal(ev[1].dur_ms, th.long_press_ms);
});

test('moving cancels a pending long press', () => {
  const { c, kinds } = rig();
  c.touch(true, 10, 10, 0);
  c.touch(true, 10 + th.tap_slop_px + 1, 10, 100);
  c.tick(th.long_press_ms + 100);
  assert.ok(!kinds().includes('long_press'));
});

test('buttons: short press fires on release, long fires from tick only once', () => {
  const { c, kinds } = rig();
  c.button(0, true, 0); c.button(0, false, 100);
  c.button(1, true, 200); c.tick(200 + th.long_press_ms); c.tick(200 + th.long_press_ms + 10);
  c.button(1, false, 1500);
  assert.deepEqual(kinds(), ['btn_a', 'btn_b_long']);
});

test('deferrer holds a navigating tap for tap_defer_ms, then fires it', () => {
  const out = [];
  const d = createDeferrer((e) => out.push(e), th, () => true);
  d.dispatch({ kind: 'tap', x: 1, y: 1 }, 100);
  assert.equal(out.length, 0);
  d.tick(100 + th.tap_defer_ms - 1, false);
  assert.equal(out.length, 0);
  d.tick(100 + th.tap_defer_ms, true); // finger down again: still held
  assert.equal(out.length, 0);
  d.tick(100 + th.tap_defer_ms, false);
  assert.equal(out[0].kind, 'tap');
  assert.ok(out[0].deferred);
});

test('a double-tap discards the held tap; other gestures flush it first', () => {
  let out = [];
  let d = createDeferrer((e) => out.push(e.kind), th, () => true);
  d.dispatch({ kind: 'tap' }, 0);
  d.dispatch({ kind: 'double_tap' }, 200);
  assert.deepEqual(out, ['double_tap']);
  out = [];
  d = createDeferrer((e) => out.push(e.kind), th, () => true);
  d.dispatch({ kind: 'tap' }, 0);
  d.dispatch({ kind: 'press' }, 50); // raw events don't flush
  d.dispatch({ kind: 'swipe_l' }, 80);
  assert.deepEqual(out, ['press', 'tap', 'swipe_l']);
});

test('taps that do not navigate are dispatched immediately', () => {
  const out = [];
  const d = createDeferrer((e) => out.push(e.kind), th, () => false);
  d.dispatch({ kind: 'tap' }, 0);
  assert.deepEqual(out, ['tap']);
});
