// Friendly (symbolic) ⇄ wire (integer-keyed CBOR) conversion for the AgentLink protocol,
// per protocol/agentlink.cddl. Symbolic form matches protocol/vectors/*.json:
// { t: 'hello', id, re?, b: {...} } with named keys and enum names.
import { encode, decode } from './cbor.js';

const ENUMS = {
  device_class: { watch: 0, desk: 1, pad: 2, sim: 3, other: 15 },
  input_kind: { touch: 0, btn: 1, rotary: 2, imu: 3 },
  feature: { focus: 0, ack: 1, approve: 2, reply: 3, voice: 4, badge: 5, debug: 6 },
  privacy: { meta: 0, preview: 1, full: 2 },
  alert_mode: { full: 0, quiet: 1, silent: 2 },
  agent: { copilot: 0, claude: 1, cloud: 2, other: 15 },
  sess_state: { start: 0, work: 1, input: 2, error: 3, done: 4, idle: 5, ended: 6 },
  input_sub: { approval: 0, question: 1 },
  att_kind: { approval: 0, question: 1, error: 2, done: 3 },
  action: { approve: 0, deny: 1, focus: 2, reply: 3, ack: 4 },
  cmd_op: { focus: 0, ack: 1, snooze: 2, approve: 3, deny: 4, reply: 5, cancel: 6, detail: 7, notify_done: 8, quiet: 9 },
  disp_shape: { round: 0, rect: 1 },
  host_os: { mac: 0, win: 1, linux: 2 },
  clear_why: { answered_pc: 0, answered_dev: 1, expired: 2, cancelled: 3 },
  result_err: { stale: 0, denied: 1, unsupported: 2, no_window: 3, timeout: 4, internal: 5 },
  log_level: { err: 0, warn: 1, info: 2, debug: 3 },
  audio_codec: { adpcm16k: 0, opus16k: 1 },
  input_event: { tap: 0, double_tap: 1, long_press: 2, swipe_up: 3, swipe_down: 4, swipe_left: 5, swipe_right: 6, btn_a: 7, btn_b: 8, btn_a_long: 9, btn_b_long: 10, lift: 11, shake: 12 },
};

export const MSG_TYPES = {
  hello: 1, welcome: 2, snapshot: 3, session_upd: 4, gone: 5, attention: 6, clear: 7, cmd: 8,
  result: 9, detail: 10, cfg: 11, batt: 12, ping: 13, pong: 14, log: 15, audio_start: 16,
  audio: 17, audio_end: 18, transcript: 19, dbg_ui_state_req: 64, dbg_ui_state: 65,
  dbg_screenshot_req: 66, dbg_screenshot_chunk: 67, dbg_inject_input: 68, dbg_set_log_level: 69,
};
const MT_NAME = Object.fromEntries(Object.entries(MSG_TYPES).map(([k, v]) => [v, k]));

// field: name → [wire key, type]. Types: uint int tstr bstr hex bool any ints tstrs
// enum:<e> enums:<e> obj:<spec> objs:<spec>
const SPECS = {
  empty: {},
  device_info: { class: [0, 'enum:device_class'], board: [1, 'tstr'], fw: [2, 'tstr'], uid: [3, 'hex'] },
  display_caps: { shape: [0, 'enum:disp_shape'], w: [1, 'uint'], h: [2, 'uint'], color: [3, 'bool'] },
  caps: {
    disp: [0, 'obj:display_caps'], input: [1, 'enums:input_kind'], nbtn: [2, 'uint'], haptic: [3, 'bool'],
    spk: [4, 'bool'], mic: [5, 'bool'], batt: [6, 'bool'], max_msg: [7, 'uint'], feats: [8, 'enums:feature'],
  },
  hello: { proto: [0, 'ints'], dev: [1, 'obj:device_info'], caps: [2, 'obj:caps'], batt_pct: [3, 'uint'] },
  host_info: { id: [0, 'hex'], name: [1, 'tstr'], os: [2, 'enum:host_os'] },
  time_sync: { unix: [0, 'uint'], tz_off: [1, 'int'] },
  device_cfg: { privacy: [0, 'enum:privacy'], alert: [1, 'enum:alert_mode'], quiet_until: [2, 'uint'], brightness: [3, 'uint'], badge_handle: [4, 'tstr'] },
  welcome: { proto: [0, 'ints'], host: [1, 'obj:host_info'], time: [2, 'obj:time_sync'], cfg: [3, 'obj:device_cfg'] },
  away_event: { sid: [0, 'uint'], kind: [1, 'enum:att_kind'], ts: [2, 'uint'] },
  session: {
    sid: [0, 'uint'], ag: [1, 'enum:agent'], st: [2, 'enum:sess_state'], sub: [3, 'enum:input_sub'],
    repo: [4, 'tstr'], br: [5, 'tstr'], tool: [6, 'tstr'], t0: [7, 'uint'], tst: [8, 'uint'],
    sum: [9, 'tstr'], seen: [10, 'bool'], host: [11, 'uint'],
  },
  snapshot: { sessions: [0, 'objs:session'], away: [1, 'objs:away_event'] },
  gone: { sid: [0, 'uint'] },
  attention: {
    rid: [0, 'uint'], sid: [1, 'uint'], kind: [2, 'enum:att_kind'], title: [3, 'tstr'], text: [4, 'tstr'],
    opts: [5, 'tstrs'], acts: [6, 'enums:action'], exp: [7, 'uint'], grant_mask: [8, 'uint'],
  },
  clear: { rid: [0, 'uint'], why: [1, 'enum:clear_why'] },
  cmd: { op: [0, 'enum:cmd_op'], sid: [1, 'uint'], rid: [2, 'uint'], arg: [3, 'any'], hold_ms: [4, 'uint'] },
  result: { ok: [0, 'bool'], err: [1, 'enum:result_err'], msg: [2, 'tstr'] },
  detail: { rid: [0, 'uint'], sid: [1, 'uint'], text: [2, 'tstr'] },
  batt: { pct: [0, 'uint'], chg: [1, 'bool'] },
  log: { lvl: [0, 'enum:log_level'], msg: [1, 'tstr'] },
  audio_start: { aid: [0, 'uint'], codec: [1, 'enum:audio_codec'], sid: [2, 'uint'] },
  audio: { aid: [0, 'uint'], seq: [1, 'uint'], data: [2, 'bstr'] },
  audio_end: { aid: [0, 'uint'] },
  transcript: { aid: [0, 'uint'], text: [1, 'tstr'], autosend_ms: [2, 'uint'] },
  dbg_ui_state: { screen: [0, 'tstr'], sel_sid: [1, 'uint'], rid: [2, 'uint'], visible: [3, 'tstrs'], power: [4, 'tstr'], fps: [5, 'uint'] },
  dbg_screenshot_chunk: { w: [0, 'uint'], h: [1, 'uint'], offset: [2, 'uint'], data: [3, 'bstr'], last: [4, 'bool'] },
  dbg_inject_input: { kind: [0, 'enum:input_event'], x: [1, 'int'], y: [2, 'int'] },
  dbg_set_log_level: { lvl: [0, 'enum:log_level'] },
};
const BODY_SPEC = {
  session_upd: 'session', cfg: 'device_cfg', ping: 'empty', pong: 'empty', dbg_ui_state_req: 'empty',
  dbg_screenshot_req: 'empty',
};
const bodySpec = (t) => BODY_SPEC[t] ?? t;

const toHex = (u) => [...u].map((b) => b.toString(16).padStart(2, '0')).join('');
const fromHex = (s) => Uint8Array.from(s.match(/../g) ?? [], (h) => parseInt(h, 16));
const invert = (o) => Object.fromEntries(Object.entries(o).map(([k, v]) => [v, k]));

function toWire(type, v) {
  const [kind, arg] = type.split(':');
  switch (kind) {
    case 'hex': return fromHex(v);
    case 'enum': return ENUMS[arg][v] ?? v;
    case 'enums': return v.map((x) => ENUMS[arg][x] ?? x);
    case 'obj': return objToWire(arg, v);
    case 'objs': return v.map((x) => objToWire(arg, x));
    default: return v;
  }
}
function fromWire(type, v) {
  const [kind, arg] = type.split(':');
  switch (kind) {
    case 'hex': return toHex(v);
    case 'enum': return invert(ENUMS[arg])[v] ?? v;
    case 'enums': return v.map((x) => invert(ENUMS[arg])[x] ?? x);
    case 'obj': return objFromWire(arg, v);
    case 'objs': return v.map((x) => objFromWire(arg, x));
    case 'any': return v instanceof Map ? Object.fromEntries(v) : v;
    default: return v;
  }
}
function objToWire(spec, o) {
  const m = new Map();
  for (const [name, [key, type]] of Object.entries(SPECS[spec])) {
    if (o[name] !== undefined && o[name] !== null) m.set(key, toWire(type, o[name]));
  }
  return m;
}
function objFromWire(spec, m) {
  const o = {};
  for (const [name, [key, type]] of Object.entries(SPECS[spec])) {
    if (m.has(key)) o[name] = fromWire(type, m.get(key));
  }
  return o;
}

/** Symbolic message → CBOR envelope bytes. */
export function encodeMsg(msg) {
  const t = MSG_TYPES[msg.t];
  if (t === undefined) throw new Error('unknown message type ' + msg.t);
  const env = new Map([[0, t], [1, msg.id]]);
  if (msg.re !== undefined && msg.re !== null) env.set(2, msg.re);
  env.set(3, objToWire(bodySpec(msg.t), msg.b ?? {}));
  return encode(env);
}

/** CBOR envelope bytes → symbolic message; unknown types decode as { t: 'type:N', raw }. */
export function decodeMsg(bytes) {
  const env = decode(bytes);
  if (!(env instanceof Map)) throw new Error('envelope is not a map');
  const code = env.get(0);
  const t = MT_NAME[code];
  const msg = { t: t ?? `type:${code}`, id: env.get(1) };
  if (env.has(2)) msg.re = env.get(2);
  msg.b = t ? objFromWire(bodySpec(t), env.get(3) ?? new Map()) : { raw: env.get(3) };
  return msg;
}

// ───────────── builders ─────────────
const PROTO = [0, 1];
export const hello = (id, o = {}) => ({
  t: 'hello', id,
  b: {
    proto: PROTO,
    dev: { class: 'watch', board: 'm5stopwatch', fw: '0.1.0', uid: '0102030405060708', ...o.dev },
    caps: {
      disp: { shape: 'round', w: 466, h: 466, color: true }, input: ['touch', 'btn', 'imu'], nbtn: 2,
      haptic: true, spk: true, mic: true, batt: true, max_msg: 4096, feats: ['focus', 'ack', 'approve', 'badge', 'debug'],
      ...o.caps,
    },
  },
});
export const welcome = (id, re, o = {}) => ({
  t: 'welcome', id, re,
  b: {
    proto: PROTO,
    host: { id: 'a1a2a3a4a5a6a7a8', name: 'your-laptop', os: 'mac', ...o.host },
    time: o.time ?? { unix: Math.floor(Date.now() / 1000), tz_off: 0 },
    cfg: { privacy: 'preview', alert: 'full', ...o.cfg },
  },
});
export const snapshot = (id, sessions, re) => ({ t: 'snapshot', id, ...(re !== undefined && { re }), b: { sessions } });
export const sessionUpd = (id, delta) => ({ t: 'session_upd', id, b: delta });
export const gone = (id, sid) => ({ t: 'gone', id, b: { sid } });
export const attention = (id, b) => ({ t: 'attention', id, b });
export const clear = (id, rid, why = 'answered_pc') => ({ t: 'clear', id, b: { rid, why } });
export const cmd = (id, op, o = {}) => ({ t: 'cmd', id, b: { op, ...o } });
export const result = (id, re, ok, err, msg) => ({ t: 'result', id, re, b: { ok, ...(err && { err }), ...(msg && { msg }) } });
export const ping = (id) => ({ t: 'ping', id, b: {} });
export const pong = (id, re) => ({ t: 'pong', id, re, b: {} });

// ───────────── description ─────────────
const STATE_WORD = { start: 'starting', work: 'working', input: 'waiting for you', error: 'in error', done: 'Done', idle: 'idle', ended: 'ended' };
const AGENT_NAME = { copilot: 'Copilot CLI', claude: 'Claude Code', cloud: 'a cloud agent', other: 'an agent' };
const DEVICE_SENT = new Set(['hello', 'cmd', 'ping', 'batt', 'log', 'dbg_ui_state', 'dbg_screenshot_chunk', 'audio_start', 'audio', 'audio_end']);

/** Short human sentence for a decoded message. dir: 'tx' (watch sent) | 'rx' (watch received). */
export function describe(msg, dir) {
  const fromWatch = dir ? dir === 'tx' : DEVICE_SENT.has(msg.t);
  const who = fromWatch ? 'Watch → daemon' : 'Daemon → watch';
  const b = msg.b ?? {};
  let what;
  switch (msg.t) {
    case 'hello': what = `hello: “I'm a ${b.dev?.board ?? 'device'}, ${b.caps?.disp?.w ?? '?'}×${b.caps?.disp?.h ?? '?'}, fw ${b.dev?.fw ?? '?'}”`; break;
    case 'welcome': what = `welcome from ${b.host?.name ?? 'host'} (privacy ${b.cfg?.privacy ?? '?'})`; break;
    case 'snapshot': what = `snapshot of ${b.sessions?.length ?? 0} session${b.sessions?.length === 1 ? '' : 's'}`; break;
    case 'session_upd': {
      const bits = [];
      if (b.st) bits.push(`is now ${STATE_WORD[b.st] ?? b.st}`);
      else if (b.tool) bits.push(`runs ${b.tool}`);
      what = `session ${b.sid}${b.ag ? ` (${AGENT_NAME[b.ag]})` : ''} ${bits.join(' ') || 'changed'}`;
      break;
    }
    case 'gone': what = `session ${b.sid} ended`; break;
    case 'attention': what = `${b.kind ?? 'attention'} request for session ${b.sid}${b.title ? `: ${b.title}` : ''}`; break;
    case 'clear': what = `request ${b.rid} cleared (${String(b.why).replace('_', ' ')})`; break;
    case 'cmd': what = `command ${b.op}${b.sid !== undefined ? ` on session ${b.sid}` : ''}${b.hold_ms ? ` after a ${b.hold_ms} ms hold` : ''}`; break;
    case 'result': what = b.ok ? `result: OK${b.msg ? ` (${b.msg})` : ''}` : `result: ${b.err ?? 'failed'}${b.msg ? ` (${b.msg})` : ''}`; break;
    case 'ping': what = 'ping (keepalive)'; break;
    case 'pong': what = 'pong (link is alive)'; break;
    case 'batt': what = `battery ${b.pct}%${b.chg ? ' charging' : ''}`; break;
    case 'log': what = `log: ${b.msg}`; break;
    case 'detail': what = `full text for ${b.rid !== undefined ? 'request ' + b.rid : 'session ' + b.sid}`; break;
    case 'cfg': what = 'new device settings'; break;
    default: what = msg.t;
  }
  return `${who}: ${what}`;
}
