import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { encode, decode, spans } from '../js/proto/cbor.js';
import { crc16 } from '../js/proto/crc16.js';
import { cobsEncode, cobsDecode, cobsLayout, encodeFrame, FrameDecoder, MAX_PAYLOAD } from '../js/proto/cobs.js';
import { fragment, Reassembler, FIRST, LAST } from '../js/proto/blefrag.js';
import { encodeMsg, decodeMsg, describe } from '../js/proto/messages.js';

const vecDir = join(dirname(fileURLToPath(import.meta.url)), '../../protocol/vectors');
const vectors = readdirSync(vecDir).filter((f) => f.endsWith('.json')).map((f) => [f, JSON.parse(readFileSync(join(vecDir, f), 'utf8'))]);
const hex = (u) => Buffer.from(u).toString('hex');
const unhex = (s) => Uint8Array.from(Buffer.from(s, 'hex'));
const pattern = (n) => Uint8Array.from({ length: n }, (_, i) => (i * 7 + 3) & 0xff);

test('vectors found', () => assert.ok(vectors.length >= 12));

for (const [name, v] of vectors) {
  test(`vector ${name}: raw CBOR round-trips`, () => {
    const bytes = unhex(v.cbor_hex);
    assert.equal(hex(encode(decode(bytes))), v.cbor_hex);
    assert.ok(spans(bytes).length > 0);
  });
  test(`vector ${name}: symbolic ⇄ wire`, () => {
    const bytes = unhex(v.cbor_hex);
    assert.deepEqual(decodeMsg(bytes), v.symbolic);
    assert.equal(hex(encodeMsg(v.symbolic)), v.cbor_hex);
    assert.ok(describe(v.symbolic).includes('→'));
  });
}

test('describe wording', () => {
  assert.equal(describe({ t: 'session_upd', id: 1, b: { sid: 2, st: 'done' } }, 'rx'), 'Daemon → watch: session 2 is now Done');
});

test('cbor ints, floats, nesting', () => {
  for (const n of [0, 23, 24, 255, 256, 65535, 65536, 4294967295, 4294967296, -1, -24, -25, -1000]) {
    assert.equal(decode(encode(n)), n);
  }
  assert.equal(decode(encode(1.5)), 1.5);
  assert.deepEqual(decode(encode([true, false, null, 'é', Uint8Array.of(1, 2)])), [true, false, null, 'é', Uint8Array.of(1, 2)]);
  assert.equal(hex(encode(1793995200)), '1a6aee31c0');
  assert.throws(() => decode(Uint8Array.of(0x82, 1)));
});

test('crc16 known vector', () => assert.equal(crc16(new TextEncoder().encode('123456789')), 0x29b1));

test('cobs edge cases round-trip', () => {
  const cases = [[], [0], [0, 0], [1, 0, 2], ...[253, 254, 255, 508, 600].map((n) => new Array(n).fill(7))];
  cases.push([...new Array(254).fill(9), 0, ...new Array(254).fill(3)]);
  cases.push(Array.from({ length: 2000 }, (_, i) => i % 256));
  for (const c of cases) {
    const u = Uint8Array.from(c);
    const e = cobsEncode(u);
    assert.ok(!e.includes(0));
    assert.deepEqual(cobsDecode(e), u, `len ${c.length}`);
  }
  assert.deepEqual([...cobsEncode([0])], [1, 1]);
  assert.deepEqual([...cobsEncode([1, 2, 0, 3])], [3, 1, 2, 2, 3]);
  assert.equal(cobsDecode(Uint8Array.of(5, 1, 2)), null);
  assert.equal(cobsDecode(Uint8Array.of(3, 1, 0)), null);
});

test('usb frames split from a byte stream and reject bad CRC', () => {
  const a = Uint8Array.of(0xa1, 0, 0, 5);
  const b = new Uint8Array(300).fill(7);
  const stream = [...encodeFrame(a), ...encodeFrame(b)];
  const d = new FrameDecoder();
  const got = [];
  for (const x of stream) got.push(...d.feed([x]));
  assert.deepEqual(got, [a, b]);
  const bad = encodeFrame(Uint8Array.of(1, 2, 3, 4));
  bad[bad.length - 3] ^= 0x55;
  assert.deepEqual(new FrameDecoder().feed(bad), []);
  assert.equal(encodeFrame(a)[0], 0);
  assert.equal(encodeFrame(a).at(-1), 0);
  const big = new FrameDecoder().feed(encodeFrame(new Uint8Array(MAX_PAYLOAD).fill(1)));
  assert.equal(big.length, 1);
});

test('ble fragmentation round trip', () => {
  for (const chunk of [2, 3, 20, 23, 185, 244]) {
    for (const len of [1, 19, 20, 21, 245, 1000, 4096]) {
      const msg = pattern(len);
      const frags = fragment(msg, chunk);
      assert.ok(frags.every((f) => f.length <= chunk));
      assert.equal(frags[0][0] & FIRST, FIRST);
      assert.equal(frags.at(-1)[0] & LAST, LAST);
      const r = new Reassembler();
      const out = frags.map((f) => r.push(f)).filter(Boolean);
      assert.deepEqual(out, [msg]);
    }
  }
});

test('ble single fragment, seq wrap, loss recovery', () => {
  assert.deepEqual(fragment(Uint8Array.of(1, 2, 3), 20), [Uint8Array.of(FIRST | LAST, 1, 2, 3)]);
  const frags = fragment(pattern(200), 2);
  assert.equal(frags[63][0] & 0x3f, 63);
  assert.equal(frags[64][0] & 0x3f, 0);
  const lossy = fragment(pattern(100), 20);
  lossy.splice(2, 1);
  const r = new Reassembler();
  assert.ok(lossy.every((f) => r.push(f) === null));
  const next = pattern(50);
  assert.deepEqual(fragment(next, 20).map((f) => r.push(f)).filter(Boolean), [next]);
  assert.equal(fragment(pattern(4097), 244), null);
  assert.equal(fragment(new Uint8Array(0), 20), null);
});

test('cobsLayout matches cobsEncode and tracks sources', () => {
  const d = Uint8Array.of(1, 0, 2, 3, 0, 0, 4);
  const lay = cobsLayout(d);
  assert.deepEqual(Uint8Array.from(lay, (x) => x.v), cobsEncode(d));
  assert.deepEqual(lay.map((x) => x.src), [-1, 0, -1, 2, 3, -1, -1, 6]);
  const big = Uint8Array.from({ length: 600 }, (_, i) => (i % 251) + 1);
  assert.deepEqual(Uint8Array.from(cobsLayout(big), (x) => x.v), cobsEncode(big));
});
