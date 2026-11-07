import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createClassifier, angleDeg } from '../js/gestures/classify.js';

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

test('a second tap is just another tap: no double-tap, no deferral', () => {
  const { c, kinds } = rig();
  c.touch(true, 230, 230, 0);
  c.touch(false, 230, 230, 60);
  c.touch(true, 235, 232, 100);
  c.touch(false, 235, 232, 150);
  assert.deepEqual(kinds(), ['press', 'release', 'tap', 'press', 'release', 'tap']);
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

test('buttons: B fires on release, B long once from tick, A waits out the double-press window', () => {
  const { c, kinds } = rig();
  c.button(1, true, 0); c.button(1, false, 50);
  assert.deepEqual(kinds(), ['btn_b']);
  c.button(1, true, 200); c.tick(200 + th.long_press_ms); c.tick(200 + th.long_press_ms + 10);
  c.button(1, false, 1500);
  assert.deepEqual(kinds(), ['btn_b', 'btn_b_long']);
  c.button(0, true, 2000); c.button(0, false, 2080);
  c.tick(2080 + th.btn_double_ms - 1);
  assert.equal(kinds().length, 2);
  c.tick(2080 + th.btn_double_ms);
  assert.equal(kinds().at(-1), 'btn_a');
});

test('A double-press inside the window gives btn_a_double and no btn_a', () => {
  const { c, kinds } = rig();
  c.button(0, true, 0); c.button(0, false, 60);
  c.button(0, true, 60 + th.btn_double_ms); c.button(0, false, 60 + th.btn_double_ms + 40);
  c.tick(2000);
  assert.deepEqual(kinds(), ['btn_a_double']);
  const r = rig(); // too late: two separate single presses
  r.c.button(0, true, 0); r.c.button(0, false, 60);
  r.c.tick(60 + th.btn_double_ms);
  r.c.button(0, true, 700); r.c.button(0, false, 760); r.c.tick(2000);
  assert.deepEqual(r.kinds(), ['btn_a', 'btn_a']);
});

test('A+B chord swallows both presses', () => {
  const { c, kinds } = rig();
  c.button(0, true, 0); c.button(1, true, 30);
  c.button(1, false, 200); c.button(0, false, 210);
  c.tick(3000);
  assert.deepEqual(kinds(), ['chord']);
});

// Finger positions on the rim at a clock angle (0 = 12 o'clock, clockwise).
const rim = (deg, r = 195) => {
  const a = (deg - 90) * Math.PI / 180;
  return [th.cx_px + r * Math.cos(a), th.cy_px + r * Math.sin(a)];
};
function dragRim(c, from, to, step, r = 195, t0 = 0) {
  let t = t0, dir = to >= from ? 1 : -1;
  c.touch(true, ...rim(from, r), t);
  for (let d = from + dir * step; dir * (to - d) >= 0; d += dir * step) c.touch(true, ...rim(d, r), t += 10);
  c.touch(false, ...rim(to, r), t += 10);
}

test('angleDeg: 12 o\'clock is 0, 3 o\'clock is 90, and it wraps', () => {
  assert.ok(Math.abs(angleDeg(th, th.cx_px, 0).deg) < 1e-6);
  assert.ok(Math.abs(angleDeg(th, 466, th.cy_px).deg - 90) < 1e-6);
  assert.ok(Math.abs(angleDeg(th, 0, th.cy_px).deg - 270) < 1e-6);
});

test('a drag along the rim past scrub_start_deg scrubs; sectors change only past the hysteresis', () => {
  const { c, ev, kinds } = rig();
  c.setSectors(4); // 90 degree sectors; start the finger in sector 0
  dragRim(c, 10, 250, 3);
  const k = kinds();
  assert.equal(k[0], 'press');
  assert.equal(k[1], 'scrub_start');
  assert.equal(ev[1].x, 0);
  assert.ok(ev[1].dur_ms >= th.scrub_start_deg);
  const scrubs = ev.filter((e) => e.kind === 'scrub');
  assert.deepEqual(scrubs.map((e) => e.x), [1, 2]);
  // 0 -> 1 only once the finger is scrub_hyst_deg (at most a third of a sector) beyond the boundary at 90 deg
  const edge = 90 + Math.min(th.scrub_hyst_deg, 90 / 3);
  assert.ok(scrubs[0].y > edge && scrubs[0].y <= edge + 3);
  assert.deepEqual(k.slice(-2), ['release', 'scrub_end']);
  assert.ok(!k.includes('tap') && !k.includes('swipe_l') && !k.includes('swipe_r'));
});

test('hysteresis: wobbling on a sector boundary does not flutter', () => {
  const { c, kinds } = rig();
  c.setSectors(4);
  c.touch(true, ...rim(60), 0);
  for (let d = 60; d <= 92; d += 4) c.touch(true, ...rim(d), d * 5);
  for (let i = 0; i < 6; i++) { c.touch(true, ...rim(89), 300 + i * 10); c.touch(true, ...rim(94), 305 + i * 10); }
  assert.equal(kinds().filter((x) => x === 'scrub').length, 0);
});

test('scrubbing counter-clockwise reports the arc and the lower sector', () => {
  const { c, ev } = rig();
  c.setSectors(4);
  dragRim(c, 130, 10, 3);
  assert.equal(ev.find((e) => e.kind === 'scrub_start').x, 1);
  assert.equal(ev.filter((e) => e.kind === 'scrub').at(-1).x, 0);
  assert.ok(ev.at(-1).kind === 'scrub_end' && ev.at(-1).dur_ms >= 80);
});

test('a drag that starts in the centre, or away from the annulus, never scrubs', () => {
  let r = rig();
  r.c.setSectors(4);
  dragRim(r.c, 10, 160, 3, 100); // r = 100 is inside rim_in_px
  assert.ok(!r.kinds().some((k) => k.startsWith('scrub')));
  r = rig();
  r.c.setSectors(4);
  dragRim(r.c, 10, 160, 3, th.rim_in_px - 1);
  assert.ok(!r.kinds().some((k) => k.startsWith('scrub')));
});

test('leaving the tracking band before the scrub starts disqualifies the touch; after it starts, it keeps going', () => {
  let r = rig();
  r.c.setSectors(4);
  r.c.touch(true, ...rim(10), 0);
  r.c.touch(true, ...rim(14, th.rim_track_out_px + 5), 10); // out of band
  r.c.touch(true, ...rim(60), 20);
  r.c.touch(false, ...rim(60), 30);
  assert.ok(!r.kinds().some((k) => k.startsWith('scrub')));
  r = rig();
  r.c.setSectors(4);
  r.c.touch(true, ...rim(10), 0);
  r.c.touch(true, ...rim(30), 10); // starts
  r.c.touch(true, ...rim(60, th.rim_track_out_px + 5), 20); // drifts out: ignored, still scrubbing
  r.c.touch(true, ...rim(130), 30);
  assert.ok(r.c.scrubbing);
  assert.equal(r.kinds().filter((k) => k === 'scrub').length, 1);
});

test('a scrub suppresses tap, swipe and long press', () => {
  const { c, kinds } = rig();
  c.setSectors(3);
  c.touch(true, ...rim(10), 0);
  c.touch(true, ...rim(40), 20);
  c.tick(th.long_press_ms + 100);
  c.touch(false, ...rim(40), 1000);
  assert.ok(!kinds().includes('long_press') && !kinds().includes('tap'));
});

test('with fewer than two sectors a scrub starts but never changes sector', () => {
  const { c, kinds } = rig();
  c.setSectors(1);
  dragRim(c, 10, 200, 5);
  assert.ok(kinds().includes('scrub_start'));
  assert.ok(!kinds().includes('scrub'));
});

test('swipe down across the centre is still a swipe (not a rim candidate)', () => {
  const { c, kinds } = rig();
  c.setSectors(4);
  c.touch(true, 233, 160, 0); c.touch(true, 233, 200, 40); c.touch(true, 233, 260, 80);
  c.touch(false, 233, 260, 90);
  assert.equal(kinds().at(-1), 'swipe_d');
});

test('a hold in the centre fires long_press from tick; a centre tap is immediate', () => {
  const { c, kinds } = rig();
  c.touch(true, 233, 233, 0); c.touch(false, 233, 233, 80);
  assert.equal(kinds().at(-1), 'tap');
});

