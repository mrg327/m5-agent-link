// COBS plus the USB frame: 0x00 ‖ COBS(payload ‖ crc16_be) ‖ 0x00 (daemon usb/frame.rs).
import { crc16 } from './crc16.js';

export const MAX_PAYLOAD = 4096;

export function cobsEncode(data) {
  const out = [0];
  let codeAt = 0;
  let code = 1;
  for (const b of data) {
    if (b === 0) {
      out[codeAt] = code;
      codeAt = out.length;
      out.push(0);
      code = 1;
    } else {
      out.push(b);
      code++;
      if (code === 0xff) {
        out[codeAt] = code;
        codeAt = out.length;
        out.push(0);
        code = 1;
      }
    }
  }
  out[codeAt] = code;
  return Uint8Array.from(out);
}

/** Same output as cobsEncode, but each byte says where it came from: src = index into
 *  data, or -1 for a COBS code byte. Used by the inspector to animate the regrouping. */
export function cobsLayout(data) {
  const out = [{ v: 0, src: -1 }];
  let codeAt = 0;
  let code = 1;
  data.forEach((b, i) => {
    if (b === 0) {
      out[codeAt].v = code;
      codeAt = out.length;
      out.push({ v: 0, src: -1 });
      code = 1;
    } else {
      out.push({ v: b, src: i });
      code++;
      if (code === 0xff) {
        out[codeAt].v = code;
        codeAt = out.length;
        out.push({ v: 0, src: -1 });
        code = 1;
      }
    }
  });
  out[codeAt].v = code;
  return out;
}

/** Returns null for invalid COBS (a zero byte or a block overrunning the input). */
export function cobsDecode(data) {
  const out = [];
  let i = 0;
  while (i < data.length) {
    const code = data[i];
    if (code === 0) return null;
    const end = i + code;
    if (end > data.length) return null;
    for (let j = i + 1; j < end; j++) {
      if (data[j] === 0) return null;
      out.push(data[j]);
    }
    i = end;
    if (code !== 0xff && i < data.length) out.push(0);
  }
  return Uint8Array.from(out);
}

/** Full frame bytes. */
export function encodeFrame(payload) {
  return frameParts(payload).frame;
}

/** Frame plus its anatomy, for the inspector. */
export function frameParts(payload) {
  const crc = crc16(payload);
  const crcBytes = Uint8Array.of(crc >> 8, crc & 0xff);
  const body = new Uint8Array(payload.length + 2);
  body.set(payload);
  body.set(crcBytes, payload.length);
  const cobs = cobsEncode(body);
  const frame = new Uint8Array(cobs.length + 2);
  frame.set(cobs, 1);
  return { frame, payload, crc, crcBytes, body, cobs };
}

function decodeChunk(chunk) {
  const body = cobsDecode(chunk);
  if (!body || body.length < 3 || body.length - 2 > MAX_PAYLOAD) return null;
  const crc = (body[body.length - 2] << 8) | body[body.length - 1];
  const payload = body.slice(0, body.length - 2);
  return crc16(payload) === crc ? payload : null;
}

/** Incremental splitter. feed(bytes) → array of payloads; non-frame chunks are ignored. */
export class FrameDecoder {
  constructor() {
    this.buf = [];
  }
  feed(data) {
    const out = [];
    for (const b of data) {
      if (b === 0) {
        if (this.buf.length) {
          const p = decodeChunk(Uint8Array.from(this.buf));
          if (p) out.push(p);
        }
        this.buf = [];
      } else this.buf.push(b);
    }
    return out;
  }
}
