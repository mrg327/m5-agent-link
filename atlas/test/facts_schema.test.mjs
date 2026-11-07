import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const facts = JSON.parse(readFileSync(new URL('../data/facts.json', import.meta.url), 'utf8'));
const css = readFileSync(new URL('../css/tokens.generated.css', import.meta.url), 'utf8');

test('palette entries are hex colours', () => {
  for (const k of ['bg', 'surface', 'text', 'muted', 'amber', 'danger', 'success', 'accent', 'idle', 'cloud']) {
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
});

test('gesture thresholds are consistent', () => {
  const g = facts.gesture;
  for (const k of ['double_tap_ms', 'long_press_ms', 'swipe_min_px', 'swipe_max_ms', 'tap_slop_px', 'tap_defer_ms']) {
    assert.ok(Number.isInteger(g[k]) && g[k] > 0, k);
  }
  assert.ok(g.tap_defer_ms > g.double_tap_ms);
});

test('haptics have steps with duty and a kick for long pulses', () => {
  const names = Object.keys(facts.haptics).sort();
  assert.deepEqual(names, ['confirm', 'done', 'error', 'hello', 'needs_input', 'reject', 'tick']);
  for (const steps of Object.values(facts.haptics)) {
    assert.ok(steps.length > 0);
    for (const s of steps) {
      assert.ok(s.on_ms > 0 && s.off_ms >= 0 && s.duty > 0 && s.duty <= 100);
    }
    assert.equal(steps.at(-1).off_ms, 0);
  }
  assert.equal(facts.haptic_motor.kick_duty, 100);
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
  assert.ok(css.includes(`--fw-double-tap-ms: ${facts.gesture.double_tap_ms}ms;`));
  assert.ok(css.includes(`--fw-tap-defer-ms: ${facts.gesture.tap_defer_ms}ms;`));
});
