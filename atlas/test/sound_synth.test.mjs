import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { render, hold, durationMs, midiHz } from '../js/sound/synth.js';

const { sounds } = JSON.parse(readFileSync(new URL('../data/facts.json', import.meta.url), 'utf8'));

test('bank has candidates for every role, ids unique, waves known', () => {
  const ids = sounds.bank.map((s) => s.id);
  assert.equal(new Set(ids).size, ids.length);
  for (const r of sounds.roles) {
    assert.ok(sounds.bank.filter((s) => s.role === r.id).length >= 2, r.id);
    assert.equal(sounds.bank.find((s) => s.id === r.pick)?.role, r.id, `${r.id} pick`);
  }
  for (const s of sounds.bank) for (const x of s.tracks.flat()) assert.ok(sounds.waves.includes(x.wave), `${s.id} ${x.wave}`);
});

test('every sound renders audible, unclipped, short and click-free at the ends', () => {
  const limit = { done: 1500, select: 150 };
  for (const s of sounds.bank) {
    const pcm = render(s, sounds.rate);
    assert.equal(pcm.length, Math.round((durationMs(s) * sounds.rate) / 1000), s.id);
    const peak = pcm.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    assert.ok(peak > 0.1 && peak < 0.98, `${s.id} peak ${peak}`);
    assert.ok(Math.abs(pcm[0]) < 0.05 && Math.abs(pcm.at(-1)) < 0.05, `${s.id} edges`);
    if (limit[s.role]) assert.ok(durationMs(s) <= limit[s.role], `${s.id} too long`);
  }
});

test('a held square note has the expected period', () => {
  const pcm = render({ tracks: [[{ wave: 'sq50', note: 69, slide: 0, vol: 15, ms: 100, decay: 0 }]] }, 16000);
  let flips = 0;
  for (let i = 401; i < 1200; i++) if (Math.sign(pcm[i]) !== Math.sign(pcm[i - 1])) flips++;
  assert.ok(Math.abs(flips / 2 - (799 / 16000) * midiHz(69)) <= 1, `flips ${flips}`);
});

test('tracks mix and echo repeats the sound after echo_ms', () => {
  const blip = [{ wave: 'sine', note: 81, slide: 0, vol: 15, ms: 20, decay: 0 }];
  const one = render({ tracks: [blip] }, 16000);
  const two = render({ tracks: [blip, blip] }, 16000);
  assert.ok(Math.abs(two[100] - 2 * one[100]) < 1e-6);
  const wet = render({ tracks: [blip], echo: { ms: 50, pct: 50 } }, 16000);
  assert.equal(wet.length, (20 + 150) * 16);
  assert.ok(Math.abs(wet[800 + 100] - 0.5 * one[100]) < 1e-6);
});

test('hold upsamples without changing duration', () => {
  const out = hold(new Float32Array([0, 1, 0, -1]), 16000, 48000);
  assert.equal(out.length, 12);
  assert.deepEqual([...out.slice(3, 6)], [1, 1, 1]);
});
