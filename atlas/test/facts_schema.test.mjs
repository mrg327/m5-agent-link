import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const facts = JSON.parse(readFileSync(new URL('../data/facts.json', import.meta.url), 'utf8'));
const css = readFileSync(new URL('../css/tokens.generated.css', import.meta.url), 'utf8');

test('palette entries are hex colours', () => {
  for (const k of ['bg', 'surface', 'text', 'muted', 'working', 'ask', 'permission', 'done', 'error', 'idle', 'amber', 'danger', 'success', 'accent']) {
    assert.match(facts.palette[k], /^#[0-9a-f]{6}$/, k);
  }
});

test('states cover the seven CDDL states with palette colours', () => {
  assert.deepEqual(Object.keys(facts.states), ['start', 'work', 'input', 'error', 'done', 'idle', 'ended']);
  const pal = new Set(Object.values(facts.palette));
  for (const s of Object.values(facts.states)) {
    assert.ok(pal.has(s.color));
    assert.match(s.source, /:\d+$/);
  }
  assert.equal(facts.states.input.sub_colors['0'], facts.palette.permission);
  assert.equal(facts.states.input.sub_labels['0'], 'Permission');
});

test('gesture thresholds describe the ring recogniser, with no tap deferral', () => {
  const g = facts.gesture;
  for (const k of ['long_press_ms', 'swipe_min_px', 'swipe_max_ms', 'tap_slop_px', 'btn_double_ms', 'rim_in_px', 'rim_out_px',
    'rim_track_in_px', 'rim_track_out_px', 'scrub_start_deg', 'scrub_hyst_deg', 'hold_confirm_ms', 'hold_show_ms', 'centre_r_px']) {
    assert.ok(Number.isInteger(g[k]) && g[k] > 0, k);
  }
  for (const k of ['tap_defer_ms', 'double_tap_ms', 'defer_slack_ms']) assert.equal(g[k], undefined, k);
  assert.ok(g.rim_track_in_px < g.rim_in_px && g.rim_out_px < g.rim_track_out_px);
  assert.ok(g.hold_confirm_ms > g.long_press_ms && g.hold_show_ms < g.hold_confirm_ms);
});

test('ring facts: screens, per-kind shapes, pills and the gesture cases', () => {
  assert.deepEqual(facts.screens.map((s) => s.id), ['ring', 'attention']);
  assert.deepEqual(Object.keys(facts.ring.kinds), ['work', 'ask', 'perm', 'done', 'error', 'idle']);
  assert.ok(facts.ring.merge_at >= 2 && facts.ring.max_segments >= facts.ring.merge_at);
  assert.equal(facts.ring.pills.approve, 'HOLD ✓ APPROVE');
  const cases = facts.gesture_map.cases.map((c) => c.gesture);
  for (const c of ['G_TAP', 'G_LONG_PRESS', 'G_SWIPE_DOWN', 'G_SCRUB', 'G_BTN_A', 'G_BTN_A_DOUBLE', 'G_BTN_B']) assert.ok(cases.includes(c), c);
});

test('haptics have steps with duty, the rim detent and the approve ramp', () => {
  assert.deepEqual(Object.keys(facts.haptics).sort(),
    ['confirm', 'detent', 'done', 'error', 'hello', 'hold_ramp', 'permission', 'question', 'reject', 'tick']);
  for (const steps of Object.values(facts.haptics)) {
    assert.ok(steps.length > 0);
    for (const s of steps) assert.ok(s.on_ms > 0 && s.off_ms >= 0 && s.duty > 0 && s.duty <= 100);
    assert.equal(steps.at(-1).off_ms, 0);
  }
  assert.equal(facts.haptic_motor.kick_duty, 100);
  assert.deepEqual(facts.haptics.detent.map((s) => [s.on_ms, s.duty]), [[facts.haptic_motor.kick_ms, 100]]);
  const ramp = facts.haptics.hold_ramp;
  assert.ok(ramp.at(-1).on_ms > ramp[0].on_ms && ramp.at(-2).off_ms < ramp[0].off_ms);
});

test('link, usb, ble and display facts', () => {
  assert.ok(facts.link.silence_timeout_ms > facts.link.ping_interval_ms);
  assert.match(facts.usb.vid, /^0x[0-9A-F]{4}$/);
  for (const k of ['service', 'rx', 'tx']) assert.match(facts.ble[k], /^[0-9a-f-]{36}$/);
  assert.equal(facts.display.width, facts.display.height);
});

test('messages include the core protocol with directions', () => {
  const names = facts.messages.map((m) => m.name);
  for (const n of ['hello', 'welcome', 'snapshot', 'session_upd', 'attention', 'cmd', 'result']) {
    assert.ok(names.includes(n), n);
  }
  for (const m of facts.messages) assert.ok(m.dir && m.summary && m.source);
});

test('every fact has a source and the CSS tokens mirror the facts', () => {
  assert.ok(Object.keys(facts.sources).length > 30);
  for (const v of Object.values(facts.sources)) assert.match(v, /^[\w./-]+:\d+$/);
  assert.ok(css.includes(`--fw-accent: ${facts.palette.accent};`));
  assert.ok(css.includes(`--fw-hold-confirm-ms: ${facts.gesture.hold_confirm_ms}ms;`));
  assert.ok(css.includes(`--fw-permission: ${facts.palette.permission};`));
  assert.ok(!css.includes('tap-defer'));
});
