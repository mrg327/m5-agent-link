// Friendly (symbolic) ⇄ wire (integer-keyed CBOR) conversion for the AgentLink protocol,
// per protocol/agentlink.cddl. Symbolic form matches protocol/vectors/*.json:
// { t: 'hello', id, re?, b: {...} } with named keys and enum names.
import { encode, decode } from './cbor.js';

const ENUMS = {
  device_class: { watch: 0, desk: 1, pad: 2, sim: 3, other: 15 },
  input_kind: { touch: 0, btn: 1, rotary: 2, imu: 3 },
  feature: { focus: 0, ack: 1, approve: 2, reply: 3, voice: 4, badge: 5, debug: 6, scroll: 7, keys: 8 },
  privacy: { meta: 0, preview: 1, full: 2 },
  alert_mode: { full: 0, quiet: 1, silent: 2 },
  agent: { copilot: 0, claude: 1, cloud: 2, shell: 3, other: 15 },
  sess_state: { start: 0, work: 1, input: 2, error: 3, done: 4, idle: 5, ended: 6 },
  input_sub: { approval: 0, question: 1 },
  att_kind: { approval: 0, question: 1, error: 2, done: 3 },
  action: { approve: 0, deny: 1, focus: 2, reply: 3, ack: 4, dictate: 5 },
  cmd_op: { focus: 0, ack: 1, snooze: 2, approve: 3, deny: 4, reply: 5, cancel: 6, detail: 7, notify_done: 8, quiet: 9, scroll: 10, type: 11, dictate: 12, kb_pick: 13, kb_del: 14, kb_send: 15, kb_end: 16, rerun: 17, unqueue: 18, answer: 19, pointer: 20, graph: 21 },
  pointer_btn: { left: 0, right: 1 },
  scroll_unit: { lines: 0, pages: 1, to_end: 2 },
  disp_shape: { round: 0, rect: 1 },
  host_os: { mac: 0, win: 1, linux: 2 },
  clear_why: { answered_pc: 0, answered_dev: 1, expired: 2, cancelled: 3, handed_back: 4 },
  result_err: { stale: 0, denied: 1, unsupported: 2, no_window: 3, timeout: 4, internal: 5 },
  log_level: { err: 0, warn: 1, info: 2, debug: 3 },
  audio_codec: { adpcm16k: 0, opus16k: 1 },
  perm_mode: { default: 0, plan: 1, accept_edits: 2, auto: 3, dont_ask: 4, bypass: 5 },
  pr_state: { none: 0, pending: 1, approved: 2, changes: 3, draft: 4 },
  rule_scope: { session: 0, project: 1 }, // proto 0.13 (STANDING-RULES.md)
  fw_status: { go: 0, done: 1, busy: 2, bad: 3, fail: 4, stopped: 5 }, // proto 0.15 (PROTOCOL.md 6c)
  input_event: { tap: 0, double_tap: 1, long_press: 2, swipe_up: 3, swipe_down: 4, swipe_left: 5, swipe_right: 6, btn_a: 7, btn_b: 8, btn_a_long: 9, btn_b_long: 10, lift: 11, shake: 12 },
};

export const MSG_TYPES = {
  hello: 1, welcome: 2, snapshot: 3, session_upd: 4, gone: 5, attention: 6, clear: 7, cmd: 8,
  result: 9, detail: 10, cfg: 11, batt: 12, ping: 13, pong: 14, log: 15, audio_start: 16,
  audio: 17, audio_end: 18, transcript: 19, kb_start: 20, kb_stroke: 21, kb_cands: 22,
  usage: 23, fw_begin: 24, fw_chunk: 25, fw_ack: 26, power: 27, graph: 28,
  dbg_ui_state_req: 64, dbg_ui_state: 65,
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
  welcome: { proto: [0, 'ints'], host: [1, 'obj:host_info'], time: [2, 'obj:time_sync'], cfg: [3, 'obj:device_cfg'], replies: [4, 'tstrs'], voice: [5, 'bool'], keys: [6, 'bool'], pointer: [7, 'bool'] },
  away_event: { sid: [0, 'uint'], kind: [1, 'enum:att_kind'], ts: [2, 'uint'] },
  session: {
    sid: [0, 'uint'], ag: [1, 'enum:agent'], st: [2, 'enum:sess_state'], sub: [3, 'enum:input_sub'],
    repo: [4, 'tstr'], br: [5, 'tstr'], tool: [6, 'tstr'], t0: [7, 'uint'], tst: [8, 'uint'],
    sum: [9, 'tstr'], seen: [10, 'bool'], host: [11, 'uint'], hn: [12, 'tstr'],
    snz: [13, 'uint'], ntf: [14, 'bool'], n: [15, 'uint'], ok: [16, 'bool'], q: [17, 'tstr'], rp: [18, 'tstrs'], eta: [19, 'uint'], exit: [20, 'uint'],
    ctx: [21, 'uint'], mdl: [22, 'tstr'], pm: [23, 'enum:perm_mode'], pr: [24, 'uint'], prs: [25, 'enum:pr_state'],
    hush: [26, 'bool'], // proto 0.12 (PRESENCE.md)
    fc: [30, 'uint'], la: [31, 'uint'], lr: [32, 'uint'], tp: [33, 'bool'], dur: [34, 'uint'], // proto 0.14 (DONE-SUMMARY.md)
    qd: [35, 'tstr'], // proto 0.17: the queued message's start
    lm: [36, 'uint'], ll: [37, 'uint'], lp: [38, 'tstr'], // proto 0.18: the last message (LAST-MESSAGE.md)
  },
  snapshot: { sessions: [0, 'objs:session'], away: [1, 'objs:away_event'] },
  gone: { sid: [0, 'uint'] },
  attention: {
    rid: [0, 'uint'], sid: [1, 'uint'], kind: [2, 'enum:att_kind'], title: [3, 'tstr'], text: [4, 'tstr'],
    opts: [5, 'tstrs'], acts: [6, 'enums:action'], exp: [7, 'uint'], grant_mask: [8, 'uint'],
    left: [9, 'uint'], win: [10, 'uint'], len: [11, 'uint'], // proto 0.11 (ANSWER-WINDOW.md)
    hush: [12, 'bool'], // proto 0.12 (PRESENCE.md)
    rule: [13, 'tstr'], scope: [14, 'enum:rule_scope'], wide: [15, 'bool'], // proto 0.13 (STANDING-RULES.md)
    qs: [16, 'objs:ask_q'], // proto 0.18: AskUserQuestion's questions (LAST-MESSAGE.md)
  },
  clear: { rid: [0, 'uint'], why: [1, 'enum:clear_why'] },
  // cmd_arg = uint / tstr / scroll_arg: a map argument is the scroll one
  pointer_arg: { dx: [0, 'int'], dy: [1, 'int'], btn: [2, 'enum:pointer_btn'], n: [3, 'uint'], wheel: [4, 'int'] },
  scroll_arg: { unit: [0, 'enum:scroll_unit'], delta: [1, 'int'], seq: [2, 'uint'] },
  cmd: { op: [0, 'enum:cmd_op'], sid: [1, 'uint'], rid: [2, 'uint'], arg: [3, 'cmd_arg'], hold_ms: [4, 'uint'], rh: [5, 'uint'], q: [6, 'bool'], picks: [7, 'ints'], aids: [8, 'ints'], ptr: [9, 'obj:pointer_arg'] }, // rh: proto 0.13 (STANDING-RULES.md); q: proto 0.17; picks / aids: proto 0.18; ptr: proto 0.20
  scroll_pos: { lines_above: [0, 'uint'], at_bottom: [1, 'bool'], total: [2, 'uint'], preview: [3, 'tstr'] },
  result: { ok: [0, 'bool'], err: [1, 'enum:result_err'], msg: [2, 'tstr'], pos: [3, 'obj:scroll_pos'] },
  detail: { rid: [0, 'uint'], sid: [1, 'uint'], text: [2, 'tstr'], off: [3, 'uint'], total: [4, 'uint'], lm: [5, 'uint'] },
  ask_q: { h: [0, 'tstr'], q: [1, 'tstr'], o: [2, 'objs:ask_opt'], m: [3, 'bool'] },
  ask_opt: { l: [0, 'tstr'], d: [1, 'tstr'] },
  batt: { pct: [0, 'uint'], chg: [1, 'bool'] },
  log: { lvl: [0, 'enum:log_level'], msg: [1, 'tstr'] },
  audio_start: { aid: [0, 'uint'], codec: [1, 'enum:audio_codec'], sid: [2, 'uint'] },
  audio: { aid: [0, 'uint'], seq: [1, 'uint'], data: [2, 'hex'] },
  audio_end: { aid: [0, 'uint'] },
  transcript: { aid: [0, 'uint'], text: [1, 'tstr'], autosend_ms: [2, 'uint'] },
  kb_start: { kid: [0, 'uint'], sid: [1, 'uint'], keys: [2, 'hex'] },
  kb_stroke: { kid: [0, 'uint'], seq: [1, 'uint'], pts: [2, 'hex'] },
  usage: { at: [0, 'uint'], h5: [1, 'uint'], h5r: [2, 'uint'], h5o: [3, 'uint'], wk: [4, 'uint'], wkr: [5, 'uint'], wko: [6, 'uint'] },
  fw_begin: { size: [0, 'uint'], sha: [1, 'hex'], ver: [2, 'tstr'], patch: [3, 'obj:fw_patch'] }, // patch: proto 0.21
  fw_patch: { psize: [0, 'uint'], base: [1, 'hex'], bsize: [2, 'uint'] },
  fw_chunk: { off: [0, 'uint'], data: [1, 'hex'] },
  fw_ack: { st: [0, 'enum:fw_status'], next: [1, 'uint'], msg: [2, 'tstr'] },
  // proto 0.19 (POWER.md): battery and the ms spent in each power state since the previous report
  power: { mv: [0, 'uint'], mvf: [1, 'uint'], vin: [2, 'uint'], chg: [3, 'bool'], usb: [4, 'bool'], pct: [5, 'uint'],
    on: [6, 'uint'], dim: [7, 'uint'], off: [8, 'uint'], deep: [9, 'uint'], lk: [10, 'uint'], ble: [11, 'uint'],
    itvl: [12, 'uint'], seq: [13, 'uint'], nd: [14, 'uint'] },
  // proto 0.22 (GRAPH.md): one session's recent tool calls, newest last; node bits: kind, state, lane, fork, merge
  graph: { sid: [0, 'uint'], nodes: [1, 'ints'], la: [2, 'ints'], drop: [3, 'uint'], gen: [4, 'uint'] },
  kb_cands: { kid: [0, 'uint'], seq: [1, 'uint'], cands: [2, 'tstrs'], typed: [3, 'uint'], err: [4, 'enum:result_err'] },
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
    case 'cmd_arg': return v !== null && typeof v === 'object' ? objToWire('scroll_arg', v) : v;
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
    case 'cmd_arg': return v instanceof Map ? objFromWire('scroll_arg', v) : v;
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
const AGENT_NAME = { copilot: 'Copilot CLI', claude: 'Claude Code', cloud: 'a cloud agent', shell: 'a terminal job', other: 'an agent' };
const DEVICE_SENT = new Set(['hello', 'cmd', 'ping', 'batt', 'log', 'dbg_ui_state', 'dbg_screenshot_chunk', 'audio_start', 'audio', 'audio_end', 'kb_start', 'kb_stroke']);

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
    case 'attention': what = `${b.kind ?? 'attention'} request for session ${b.sid}${b.title ? `: ${b.title}` : ''}${b.left !== undefined ? `, ${Math.round(b.left / 1000)} s left to answer` : ''}`; break;
    case 'clear': what = `request ${b.rid} cleared (${String(b.why).replaceAll('_', ' ')})`; break;
    case 'cmd': {
      const scroll = b.op === 'scroll' && b.arg && typeof b.arg === 'object'
        ? ` ${b.arg.unit === 'to_end' ? (b.arg.delta < 0 ? 'to the top' : 'to the bottom') : `${Math.abs(b.arg.delta)} ${b.arg.unit} ${b.arg.delta < 0 ? 'up' : 'down'}`}`
        : '';
      what = `command ${b.op}${scroll}${b.sid !== undefined ? ` on session ${b.sid}` : ''}${b.hold_ms ? ` after a ${b.hold_ms} ms hold` : ''}`;
      break;
    }
    case 'result': {
      const pos = b.pos ? (b.pos.at_bottom ? ', live at the bottom' : `, ${b.pos.lines_above} lines above the bottom`) : '';
      what = b.ok ? `result: OK${pos}${b.msg ? ` (${b.msg})` : ''}` : `result: ${b.err ?? 'failed'}${b.msg ? ` (${b.msg})` : ''}`;
      break;
    }
    case 'ping': what = 'ping (keepalive)'; break;
    case 'pong': what = 'pong (link is alive)'; break;
    case 'batt': what = `battery ${b.pct}%${b.chg ? ' charging' : ''}`; break;
    case 'graph': what = `agent graph for session ${b.sid}: ${(b.nodes ?? []).length} calls${b.drop ? `, ${b.drop} older dropped` : ''}`; break;
    case 'power': what = `power report: ${b.mv} mV${b.chg ? ' charging' : ''}, ${Math.round(((b.on ?? 0) + (b.dim ?? 0) + (b.off ?? 0) + (b.deep ?? 0)) / 1000)} s covered`; break;
    case 'log': what = `log: ${b.msg}`; break;
    case 'detail': what = `full text for ${b.rid !== undefined ? 'request ' + b.rid : 'session ' + b.sid}${b.total !== undefined ? `, bytes ${b.off ?? 0}-${(b.off ?? 0) + new TextEncoder().encode(b.text ?? '').length} of ${b.total}` : ''}`; break;
    case 'cfg': what = 'new device settings'; break;
    case 'usage': what = b.h5 === undefined && b.wk === undefined ? 'plan usage: not known'
      : `plan usage: 5 h ${b.h5 ?? '–'}%, week ${b.wk ?? '–'}%${b.h5o ? ', on pace to run out early' : ''}`; break;
    default: what = msg.t;
  }
  return `${who}: ${what}`;
}
