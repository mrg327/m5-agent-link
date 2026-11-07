// Packet inspector: message → CBOR bytes → transport framing, with the bytes sliding between layers.
import { spans } from '../proto/cbor.js';
import { cobsLayout, frameParts } from '../proto/cobs.js';
import { fragment, parseHeader } from '../proto/blefrag.js';
import { describe } from '../proto/messages.js';

const esc = (s) => String(s).replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
const hex2 = (v) => v.toString(16).padStart(2, '0');

function scalar(v) {
  if (typeof v === 'string') return `<i class="j-s">"${esc(v)}"</i>`;
  if (typeof v === 'number') return `<i class="j-n">${v}</i>`;
  return `<i class="j-b">${esc(v)}</i>`;
}
const isScalar = (v) => v === null || typeof v !== 'object';

/** Pretty-prints a symbolic message as HTML lines (key/value coloured). */
export function jsonLines(v, ind = 0, key = '') {
  const pad = '  '.repeat(ind);
  const k = key ? `<i class="j-k">${esc(key)}</i>: ` : '';
  if (isScalar(v)) return [`${pad}${k}${scalar(v)}`];
  if (Array.isArray(v)) {
    if (v.every(isScalar)) return [`${pad}${k}[${v.map(scalar).join(', ')}]`];
    const out = [`${pad}${k}[`];
    v.forEach((x) => out.push(...jsonLines(x, ind + 1)));
    out.push(`${pad}]`);
    return out;
  }
  const entries = Object.entries(v);
  const out = [`${pad}${k}{`];
  entries.forEach(([kk, x]) => out.push(...jsonLines(x, ind + 1, kk)));
  out.push(`${pad}}`);
  return out;
}

function visible(n, head, tail) {
  if (n <= head + tail + 4) return { show: Array.from({ length: n }, (_, i) => i), cut: null };
  return { show: [...Array.from({ length: head }, (_, i) => i), ...Array.from({ length: tail }, (_, i) => n - tail + i)], cut: [head, n - tail] };
}

export function createInspector(host, { facts, reducedMotion }) {
  host.innerHTML = `
    <div class="pk__head">
      <span class="pk__dir" data-r="dir"></span>
      <span class="pk__desc" data-r="desc"></span>
    </div>
    <div class="pk__grid">
      <section class="pk__layer pk__layer--msg" aria-labelledby="pk-l1">
        <h4 id="pk-l1"><b>1</b> The message <i>decoded</i></h4>
        <pre class="pk__json" data-r="json" tabindex="0"></pre>
      </section>
      <section class="pk__layer pk__layer--cbor" aria-labelledby="pk-l2">
        <h4 id="pk-l2"><b>2</b> CBOR <i data-r="cborN"></i></h4>
        <div class="pk__bytes" data-r="cbor" role="img" aria-label="CBOR bytes in hex"></div>
        <p class="pk__legend"><span class="by k-head">00</span> type &amp; length <span class="by k-key">00</span> map key <span class="by k-value">00</span> value</p>
      </section>
      <section class="pk__layer pk__layer--wire" aria-labelledby="pk-l3">
        <h4 id="pk-l3"><b>3</b> <span data-r="wireTitle"></span> <i data-r="wireN"></i>
          <button type="button" class="pk__again" data-r="again" aria-label="Replay the wrapping animation">↻ wrap again</button></h4>
        <div class="pk__bytes pk__bytes--wire" data-r="wire" role="img" aria-label="Bytes as sent on the transport"></div>
        <p class="pk__note" data-r="note"></p>
      </section>
    </div>`;
  const r = Object.fromEntries([...host.querySelectorAll('[data-r]')].map((n) => [n.dataset.r, n]));
  let cur = null;
  let transport = 'usb';
  let cborEls = new Map();

  const by = (v, cls, extra = {}) => {
    const s = document.createElement('span');
    s.className = `by ${cls}`;
    s.textContent = hex2(v);
    Object.assign(s.dataset, extra);
    return s;
  };
  const gap = (n, unit = 'bytes') => {
    const s = document.createElement('span');
    s.className = 'by-gap';
    s.textContent = `· · · ${n} more ${unit} · · ·`;
    return s;
  };

  function renderCbor(bytes) {
    r.cbor.replaceChildren();
    cborEls = new Map();
    const sp = spans(bytes);
    const kind = new Array(bytes.length).fill('value');
    for (const s of sp) for (let i = s.start; i < s.end; i++) kind[i] = s.kind;
    const { show, cut } = visible(bytes.length, 150, 6);
    show.forEach((i) => {
      if (cut && i === cut[1]) r.cbor.append(gap(cut[1] - cut[0]));
      const e = by(bytes[i], `k-${kind[i]}`, { i });
      cborEls.set(i, e);
      r.cbor.append(e);
    });
    r.cborN.textContent = `${bytes.length} bytes`;
  }

  function renderWire(payload) {
    r.wire.replaceChildren();
    const movers = []; // [wireEl, srcIndex | -1]
    const cls = (i) => cborEls.get(i)?.className.match(/k-\w+/)?.[0] ?? 'k-value';
    const lane = transport;
    if (lane === 'usb') {
      const { crc, crcBytes, body } = frameParts(payload);
      const lay = cobsLayout(body);
      r.wireTitle.textContent = 'USB frame';
      r.wireN.textContent = `${lay.length + 2} bytes on the wire`;
      const row = document.createElement('div');
      row.className = 'pk__row';
      row.append(by(0, 'w-delim', {}));
      const { show, cut } = visible(lay.length, 140, 8);
      for (const i of show) {
        if (cut && i === cut[1]) row.append(gap(cut[1] - cut[0]));
        const x = lay[i];
        let e;
        if (x.src < 0) e = by(x.v, 'w-code');
        else if (x.src >= payload.length) e = by(x.v, 'w-crc');
        else e = by(x.v, `w-data ${cls(x.src)}`);
        movers.push([e, x.src >= 0 && x.src < payload.length ? x.src : -1]);
        row.append(e);
      }
      row.append(by(0, 'w-delim'));
      r.wire.append(row);
      const dl = [row.firstChild, row.lastChild];
      movers.push([dl[0], -1], [dl[1], -1]);
      r.note.innerHTML = `<span class="by w-delim">00</span> delimiter · <span class="by w-code">xx</span> COBS code (removes every <code>00</code> from the data) · <span class="by w-crc">xx</span> CRC-16/CCITT-FALSE <code>${hex2(crcBytes[0])}${hex2(crcBytes[1])}</code> over the payload, big-endian.`;
      void crc;
    } else if (lane === 'ble') {
      const mtu = facts?.ble?.default_write_len ?? 20;
      const frags = fragment(payload, mtu) ?? [];
      r.wireTitle.textContent = `BLE ${frags.length === 1 ? 'fragment' : 'fragments'}`;
      r.wireN.textContent = `${frags.length} × ≤${mtu} bytes`;
      const keep = frags.length > 6 ? [0, 1, 2, frags.length - 1] : frags.map((_, i) => i);
      keep.forEach((fi, n) => {
        if (n > 0 && fi - keep[n - 1] > 1) {
          const g = document.createElement('div');
          g.className = 'pk__row pk__row--gap';
          g.append(gap(fi - keep[n - 1] - 1, 'fragments'));
          r.wire.append(g);
        }
        const f = frags[fi];
        const h = parseHeader(f[0]);
        const row = document.createElement('div');
        row.className = 'pk__row pk__row--frag';
        const hdr = by(f[0], 'w-hdr');
        row.append(hdr);
        movers.push([hdr, -1]);
        for (let j = 1; j < f.length; j++) {
          const src = fi * (mtu - 1) + (j - 1);
          const e = by(f[j], `w-data ${cls(src)}`);
          movers.push([e, src]);
          row.append(e);
        }
        const bits = document.createElement('span');
        bits.className = 'pk__bits';
        bits.innerHTML = `<b class="${h.first ? 'on' : ''}">FIRST</b><b class="${h.last ? 'on' : ''}">LAST</b><em>seq ${h.seq}</em>`;
        row.append(bits);
        r.wire.append(row);
      });
      r.note.innerHTML = `Header byte = <b>FIRST</b> bit 7 · <b>LAST</b> bit 6 · <b>seq</b> bits 0–5 (wraps at 64). The watch glues the fragments back together.`;
    } else {
      r.wireTitle.textContent = 'WebSocket message';
      r.wireN.textContent = `1 binary message · ${payload.length} bytes`;
      const row = document.createElement('div');
      row.className = 'pk__row';
      const { show, cut } = visible(payload.length, 150, 6);
      for (const i of show) {
        if (cut && i === cut[1]) row.append(gap(cut[1] - cut[0]));
        const e = by(payload[i], `w-data ${cls(i)}`);
        movers.push([e, i]);
        row.append(e);
      }
      r.wire.append(row);
      r.note.innerHTML = `A WebSocket already frames messages, so the CBOR goes out unchanged as one <b>binary</b> message. No delimiter, no CRC.`;
    }
    if (!reducedMotion) regroup(movers);
  }

  // FLIP: start each wire byte on top of its CBOR byte, then let it glide home.
  function regroup(movers) {
    const fresh = [];
    for (const [e, src] of movers) {
      const from = src >= 0 ? cborEls.get(src) : null;
      if (from) {
        const a = from.getBoundingClientRect();
        const b = e.getBoundingClientRect();
        e.style.transition = 'none';
        e.style.transform = `translate(${a.left - b.left}px, ${a.top - b.top}px)`;
        e.style.opacity = '.9';
      } else {
        e.style.transition = 'none';
        e.style.opacity = '0';
        e.style.transform = 'scale(.4)';
        fresh.push(e);
      }
    }
    void r.wire.offsetWidth;
    movers.forEach(([e, src], i) => {
      const d = Math.min(i, 160) * 3;
      e.style.transition = `transform 700ms var(--ease-out, ease-out) ${d}ms, opacity 420ms ease ${src >= 0 ? d : 450 + d / 2}ms`;
      e.style.transform = '';
      e.style.opacity = '';
    });
  }

  function show(frame) {
    cur = frame;
    const fromWatch = frame.dir === 'tx';
    r.dir.textContent = fromWatch ? '↑ watch → PC' : '↓ PC → watch';
    r.dir.dataset.dir = frame.dir;
    r.desc.textContent = describe(frame.msg, frame.dir);
    const lines = jsonLines(frame.msg);
    r.json.innerHTML = (lines.length > 26 ? [...lines.slice(0, 22), `<i class="j-dim">  … ${lines.length - 22} more lines</i>`] : lines).join('\n');
    renderCbor(frame.bytes);
    renderWire(frame.bytes);
    host.dataset.empty = 'false';
  }

  r.again.addEventListener('click', () => cur && renderWire(cur.bytes));
  host.dataset.empty = 'true';

  return {
    show,
    setTransport(k) { transport = k; host.dataset.transport = k; if (cur) renderWire(cur.bytes); },
    get current() { return cur; },
  };
}
