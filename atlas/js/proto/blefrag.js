// BLE fragmentation (daemon ble/frag.rs): each write/notify is [FIRST b7 | LAST b6 | seq b0-5] data…
export const FIRST = 0x80;
export const LAST = 0x40;
export const SEQ_MASK = 0x3f;
export const MAX_MESSAGE = 4096;

/** Splits msg into fragments of at most chunkLen bytes (header included); null if impossible. */
export function fragment(msg, chunkLen = 20) {
  if (!msg.length || msg.length > MAX_MESSAGE || chunkLen < 2) return null;
  const n = chunkLen - 1;
  const count = Math.ceil(msg.length / n);
  const out = [];
  for (let i = 0; i < count; i++) {
    const part = msg.subarray(i * n, (i + 1) * n);
    let hdr = i & SEQ_MASK;
    if (i === 0) hdr |= FIRST;
    if (i === count - 1) hdr |= LAST;
    const f = new Uint8Array(part.length + 1);
    f[0] = hdr;
    f.set(part, 1);
    out.push(f);
  }
  return out;
}

export function parseHeader(h) {
  return { first: !!(h & FIRST), last: !!(h & LAST), seq: h & SEQ_MASK };
}

export class Reassembler {
  constructor() {
    this.reset();
  }
  reset() {
    this.buf = [];
    this.active = false;
    this.nextSeq = 0;
  }
  /** Returns the whole message when the LAST fragment arrives, else null. */
  push(frag) {
    if (!frag.length) return null;
    const { first, last, seq } = parseHeader(frag[0]);
    if (first) {
      this.reset();
      this.active = true;
    } else if (!this.active || seq !== this.nextSeq) {
      this.reset();
      return null;
    }
    this.nextSeq = (seq + 1) & SEQ_MASK;
    if (this.buf.length + frag.length - 1 > MAX_MESSAGE) {
      this.reset();
      return null;
    }
    for (let i = 1; i < frag.length; i++) this.buf.push(frag[i]);
    if (last) {
      this.active = false;
      const msg = Uint8Array.from(this.buf);
      this.buf = [];
      return msg.length ? msg : null;
    }
    return null;
  }
}
