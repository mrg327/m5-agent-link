// What each input does for a session in each state on the Ring screen. Hand-written from
// docs/design/UI-RING.md and ui_home.c (ui_home_gesture): act_focus_selected, the long-press
// branches, approvable(), next_attention(). Colours, pills and timings come from facts.json.
// If ui_home_gesture() changes, extract.py fails until this is revisited.

export const INPUTS = [
  { id: 'tap', label: 'Tap centre', short: 'Tap' },
  { id: 'long_press', label: 'Long-press', short: 'Long-press' },
  { id: 'hold', label: 'Hold', short: 'Hold' },
  { id: 'swipe_d', label: 'Swipe down', short: 'Swipe ↓' },
  { id: 'a', label: 'A', short: 'A' },
  { id: 'a_double', label: 'A double', short: 'A ×2' },
  { id: 'b', label: 'B', short: 'B' },
];

const FOCUS = { fx: 'focus', text: 'Focus the terminal on the PC' };
const FOCUS_ACK = { fx: 'focus', text: 'Focus the terminal, and acknowledge it (the segment dims)' };
const NEXT_ATT = { fx: 'jump', text: 'Jump to the next session that needs you: input or permission first, then errors, then unseen Done' };
const NEXT = { fx: 'step', text: 'Select the next session (hold B: the previous one)' };
const NOTHING = (why) => ({ fx: 'none', text: why });

/** Rows: one per ring state. `kind` keys facts.ring.kinds, `palette` keys facts.palette. */
export const STATE_ROWS = [
  {
    id: 'work', kind: 'work', palette: 'working', label: 'Working', icon: '↻',
    cells: {
      tap: FOCUS, long_press: { ...FOCUS, text: 'Focus (snooze is not built yet)' },
      hold: NOTHING('Nothing more: the long-press already focused at 0.6 s'),
      swipe_d: NOTHING('Only a permission request can be denied'), a: FOCUS, a_double: NEXT_ATT, b: NEXT,
    },
  },
  {
    id: 'ask', kind: 'ask', palette: 'ask', label: 'Question', icon: '?',
    cells: {
      tap: FOCUS, long_press: { ...FOCUS, text: 'Focus (a quick reply is planned)' },
      hold: NOTHING('Nothing more: the long-press already focused at 0.6 s'),
      swipe_d: NOTHING('Only a permission request can be denied'), a: FOCUS, a_double: NEXT_ATT, b: NEXT,
    },
  },
  {
    id: 'perm', kind: 'perm', palette: 'permission', label: 'Needs approval', icon: '🔔',
    cells: {
      tap: { fx: 'focus', text: 'Focus, to review the request first. A tap never approves' },
      long_press: NOTHING('Ignored: the approve hold must run the full time'),
      hold: { fx: 'approve', text: 'Approve. The arc fills from 0.25 s, the haptic ramps, and a click confirms at 1.2 s' },
      swipe_d: { fx: 'deny', text: 'Deny the request' },
      a: { ...FOCUS, text: 'Focus, to review. A never approves' }, a_double: NEXT_ATT, b: NEXT,
    },
  },
  {
    id: 'done', kind: 'done', palette: 'done', label: 'Done', icon: '✓',
    cells: {
      tap: FOCUS_ACK, long_press: { fx: 'ack', text: 'Acknowledge only: dismiss it without opening the terminal' },
      hold: NOTHING('Nothing more: the long-press already acknowledged at 0.6 s'),
      swipe_d: NOTHING('Only a permission request can be denied'), a: FOCUS_ACK, a_double: NEXT_ATT, b: NEXT,
    },
  },
  {
    id: 'error', kind: 'error', palette: 'error', label: 'Error', icon: '!',
    cells: {
      tap: FOCUS, long_press: { fx: 'ack', text: 'Dismiss the error without opening the terminal' },
      hold: NOTHING('Nothing more: the long-press already dismissed it at 0.6 s'),
      swipe_d: NOTHING('Only a permission request can be denied'), a: FOCUS, a_double: NEXT_ATT, b: NEXT,
    },
  },
  {
    id: 'idle', kind: 'idle', palette: 'idle', label: 'Idle', icon: '·',
    cells: {
      tap: FOCUS, long_press: { ...FOCUS, text: 'Focus (un-snooze is not built yet)' },
      hold: NOTHING('Nothing more: the long-press already focused at 0.6 s'),
      swipe_d: NOTHING('Only a permission request can be denied'), a: FOCUS, a_double: NEXT_ATT, b: NEXT,
    },
  },
];

/** Default-action pill per row, from facts (the strings in ui_home.c). */
export function pillFor(row, facts) {
  const p = facts.ring.pills;
  return row.id === 'perm' ? p.approve : row.id === 'done' ? p.done : p.default;
}

export const rowColor = (row, facts) => facts.palette[row.palette];

/** Which fx tone a verdict gets: acting inputs are 'ok', no-ops are 'no'. */
export const tone = (cell) => (cell.fx === 'none' ? 'no' : cell.fx === 'deny' ? 'danger' : 'ok');
