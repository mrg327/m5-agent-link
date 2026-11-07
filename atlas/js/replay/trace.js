// Parser for ~/.agentlink/trace/*.jsonl (format: daemon/crates/agentlinkd/src/trace.rs).
// One JSON object per line: {ts_us, ts, src, dir, kind, sid?, rid?, msg, bytes?}.
// We read only the registry's session_upd / gone lines (one per real state transition), the
// attention messages sent to the device, and focus commands. Everything stays in the browser.

export function basename(p) {
  if (typeof p !== 'string') return '';
  const parts = p.split(/[\\/]+/).filter(Boolean);
  return parts.length ? parts[parts.length - 1] : p;
}

export function parseTrace(text) {
  const rows = [];
  let bad = 0;
  for (const line of String(text).split('\n')) {
    const s = line.trim();
    if (!s) continue;
    try {
      const d = JSON.parse(s);
      if (d && typeof d === 'object' && typeof d.kind === 'string') rows.push(d);
      else bad++;
    } catch { bad++; }
  }
  const tsOf = (d) => (typeof d.ts_us === 'number' ? d.ts_us / 1000 : Date.parse(d.ts));
  rows.sort((a, b) => tsOf(a) - tsOf(b));

  const hasRegistry = rows.some((d) => d.src === 'registry' && d.kind === 'session_upd');
  const sessions = new Map();
  const focus = [];
  const attention = [];
  let first = Infinity, last = -Infinity;
  const touch = (t) => { if (Number.isFinite(t)) { first = Math.min(first, t); last = Math.max(last, t); } };

  const sess = (sid) => {
    let s = sessions.get(sid);
    if (!s) sessions.set(sid, (s = { sid, ag: 'other', repo: '', br: '', steps: [], endedAt: null }));
    return s;
  };

  for (const d of rows) {
    const t = tsOf(d);
    if (!Number.isFinite(t)) continue;
    const m = d.msg || {};
    const body = m.b || m;
    if (d.kind === 'session_upd' && (hasRegistry ? d.src === 'registry' : d.dir === 'out')) {
      const sid = body.sid ?? d.sid;
      if (sid == null) continue;
      const s = sess(sid);
      if (body.ag) s.ag = body.ag;
      if (body.repo) s.repo = basename(body.repo);
      if (body.br) s.br = body.br;
      if (body.st) {
        s.steps.push({ t, st: body.st, sub: body.sub ?? null, tool: body.tool || '', patch: stripPatch(body) });
      }
      touch(t);
    } else if (d.kind === 'gone' && (d.src === 'registry' || d.dir === 'out')) {
      const sid = body.sid ?? m.sid ?? d.sid;
      if (sid != null) { sess(sid).endedAt = t; touch(t); }
    } else if (d.kind === 'attention' && d.dir === 'out') {
      attention.push({ t, sid: body.sid ?? d.sid, kind: body.kind, title: body.title || '' });
      touch(t);
    } else if (d.kind === 'focus' && d.src === 'audit') {
      focus.push({ t, sid: d.sid ?? m.cmd?.sid, ok: m.ok !== false });
      touch(t);
    } else if (d.kind === 'cmd' && d.dir === 'in' && body.op === 'focus' && !rows.some((r) => r.src === 'audit' && r.kind === 'focus')) {
      focus.push({ t, sid: body.sid ?? d.sid, ok: true });
      touch(t);
    }
  }
  const list = [...sessions.values()].filter((s) => s.steps.length).sort((a, b) => a.steps[0].t - b.steps[0].t);
  if (!Number.isFinite(first)) { first = 0; last = 0; }
  return { sessions: list, focus, attention, start: first, end: last, duration: last - first, bad, rows: rows.length };
}

// The patch handed to ctx.daemon.inject: only the keys the protocol's `session` map knows.
function stripPatch(b) {
  const out = {};
  for (const k of ['sid', 'ag', 'st', 'sub', 'repo', 'br', 'tool', 'sum', 't0', 'tst', 'seen', 'host']) {
    if (b[k] !== undefined) out[k] = b[k];
  }
  if (typeof out.repo === 'string') out.repo = basename(out.repo);
  return out;
}

// Time-ordered playable events: state changes, endings and focus commands.
export function buildTimeline(trace) {
  const ev = [];
  for (const s of trace.sessions) {
    for (const st of s.steps) ev.push({ t: st.t, type: 'patch', sid: s.sid, patch: st.patch });
    if (s.endedAt != null) ev.push({ t: s.endedAt, type: 'gone', sid: s.sid });
  }
  for (const f of trace.focus) ev.push({ t: f.t, type: 'focus', sid: f.sid, ok: f.ok });
  for (const a of trace.attention) ev.push({ t: a.t, type: 'attention', sid: a.sid, kind: a.kind, title: a.title });
  return ev.sort((a, b) => a.t - b.t);
}

// State of one session at time t: {st, sub, tool} or null before it began / after it left.
export function stateAt(session, t) {
  if (t < session.steps[0].t) return null;
  if (session.endedAt != null && t >= session.endedAt) return null;
  let cur = session.steps[0];
  for (const s of session.steps) { if (s.t <= t) cur = s; else break; }
  return cur;
}
