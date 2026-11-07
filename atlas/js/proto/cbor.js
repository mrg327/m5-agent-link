// Minimal CBOR (RFC 8949) for the AgentLink subset: uint/nint, bstr, tstr, array, map,
// bool, null, floats. Maps decode to `Map` (keys keep their type and order); the encoder
// emits canonical form (shortest ints, definite lengths, keys sorted length-first then
// bytewise), matching the vectors in protocol/vectors.

const enc = new TextEncoder();
const dec = new TextDecoder('utf-8', { fatal: true });

function head(major, n, out) {
  const m = major << 5;
  if (n < 24) out.push(m | n);
  else if (n < 0x100) out.push(m | 24, n);
  else if (n < 0x10000) out.push(m | 25, n >> 8, n & 0xff);
  else if (n < 0x100000000) out.push(m | 26, (n >>> 24) & 0xff, (n >>> 16) & 0xff, (n >>> 8) & 0xff, n & 0xff);
  else {
    let b = BigInt(n);
    out.push(m | 27);
    for (let i = 7; i >= 0; i--) out.push(Number((b >> BigInt(i * 8)) & 0xffn));
  }
}

function cmpBytes(a, b) {
  if (a.length !== b.length) return a.length - b.length;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return a[i] - b[i];
  return 0;
}

function encodeInto(v, out) {
  if (v === null || v === undefined) out.push(0xf6);
  else if (v === true) out.push(0xf5);
  else if (v === false) out.push(0xf4);
  else if (typeof v === 'number') {
    if (Number.isInteger(v)) {
      if (v >= 0) head(0, v, out);
      else head(1, -1 - v, out);
    } else {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setFloat64(0, v);
      out.push(0xfb, ...b);
    }
  } else if (typeof v === 'bigint') {
    if (v >= 0n) head(0, v, out);
    else head(1, -1n - v, out);
  } else if (typeof v === 'string') {
    const b = enc.encode(v);
    head(3, b.length, out);
    for (const x of b) out.push(x);
  } else if (v instanceof Uint8Array) {
    head(2, v.length, out);
    for (const x of v) out.push(x);
  } else if (Array.isArray(v)) {
    head(4, v.length, out);
    for (const x of v) encodeInto(x, out);
  } else if (v instanceof Map || typeof v === 'object') {
    // Plain-object keys that look like integers are integer keys.
    const entries = v instanceof Map ? [...v] : Object.entries(v).map(([k, x]) => [/^\d+$/.test(k) ? Number(k) : k, x]);
    const items = entries.filter(([, x]) => x !== undefined).map(([k, x]) => {
      const kb = [];
      encodeInto(k, kb);
      return { kb, x };
    });
    items.sort((a, b) => cmpBytes(a.kb, b.kb));
    head(5, items.length, out);
    for (const { kb, x } of items) {
      for (const b of kb) out.push(b);
      encodeInto(x, out);
    }
  } else throw new TypeError('cbor: cannot encode ' + typeof v);
}

export function encode(v) {
  const out = [];
  encodeInto(v, out);
  return Uint8Array.from(out);
}

function half(h) {
  const e = (h >> 10) & 0x1f;
  const f = h & 0x3ff;
  const s = h & 0x8000 ? -1 : 1;
  if (e === 0) return s * 2 ** -14 * (f / 1024);
  if (e === 31) return f ? NaN : s * Infinity;
  return s * 2 ** (e - 15) * (1 + f / 1024);
}

/** Decodes one item. Throws on truncated/trailing/unsupported input. */
export function decode(bytes) {
  const { value, end } = decodeAt(bytes, 0);
  if (end !== bytes.length) throw new Error('cbor: trailing bytes');
  return value;
}

export function decodeAt(b, pos) {
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let p = pos;
  const need = (n) => {
    if (p + n > b.length) throw new Error('cbor: truncated');
  };
  function arg(ai) {
    if (ai < 24) return ai;
    if (ai === 24) { need(1); return b[p++]; }
    if (ai === 25) { need(2); const v = dv.getUint16(p); p += 2; return v; }
    if (ai === 26) { need(4); const v = dv.getUint32(p); p += 4; return v; }
    if (ai === 27) {
      need(8);
      const v = dv.getBigUint64(p);
      p += 8;
      return v <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(v) : v;
    }
    throw new Error('cbor: unsupported additional info ' + ai);
  }
  function item() {
    need(1);
    const ib = b[p++];
    const major = ib >> 5;
    const ai = ib & 31;
    switch (major) {
      case 0: return arg(ai);
      case 1: { const n = arg(ai); return typeof n === 'bigint' ? -1n - n : -1 - n; }
      case 2: { const n = arg(ai); need(n); const v = b.slice(p, p + n); p += n; return v; }
      case 3: { const n = arg(ai); need(n); const v = dec.decode(b.subarray(p, p + n)); p += n; return v; }
      case 4: { const n = arg(ai); const a = []; for (let i = 0; i < n; i++) a.push(item()); return a; }
      case 5: { const n = arg(ai); const m = new Map(); for (let i = 0; i < n; i++) { const k = item(); m.set(k, item()); } return m; }
      case 7:
        if (ai === 20) return false;
        if (ai === 21) return true;
        if (ai === 22 || ai === 23) return null;
        if (ai === 25) { need(2); const v = half(dv.getUint16(p)); p += 2; return v; }
        if (ai === 26) { need(4); const v = dv.getFloat32(p); p += 4; return v; }
        if (ai === 27) { need(8); const v = dv.getFloat64(p); p += 8; return v; }
        throw new Error('cbor: unsupported simple value');
      default: throw new Error('cbor: unsupported major type ' + major);
    }
  }
  const value = item();
  return { value, end: p };
}

/**
 * Walks encoded bytes and returns a flat list of spans for the inspector:
 * [{start, end, kind: 'head'|'key'|'value', depth, label}] where head = type/length bytes.
 * Map keys (usually small ints) are tagged 'key', everything else 'value'.
 */
export function spans(bytes) {
  const out = [];
  let p = 0;
  function walk(role, depth, label) {
    const start = p;
    const ib = bytes[p++];
    const major = ib >> 5;
    const ai = ib & 31;
    let n = ai;
    if (ai === 24) n = bytes[p++];
    else if (ai === 25) { n = (bytes[p] << 8) | bytes[p + 1]; p += 2; }
    else if (ai === 26) { n = ((bytes[p] << 24) | (bytes[p + 1] << 16) | (bytes[p + 2] << 8) | bytes[p + 3]) >>> 0; p += 4; }
    else if (ai === 27) { p += 8; n = 0; }
    const headEnd = p;
    const push = (s, e, kind) => out.push({ start: s, end: e, kind, depth, label });
    if (major === 0 || major === 1 || major === 7) {
      push(start, headEnd, role);
    } else if (major === 2 || major === 3) {
      push(start, headEnd, 'head');
      if (n) push(headEnd, headEnd + n, role);
      p += n;
    } else if (major === 4) {
      push(start, headEnd, 'head');
      for (let i = 0; i < n; i++) walk('value', depth + 1, label);
    } else if (major === 5) {
      push(start, headEnd, 'head');
      for (let i = 0; i < n; i++) {
        walk('key', depth + 1, label);
        walk('value', depth + 1, label);
      }
    }
  }
  walk('value', 0, '');
  return out;
}
