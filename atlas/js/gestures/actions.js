// The six states a session shows on the ring: one practice segment each in the gesture lab. Labels and
// colours come from facts (ui_theme.c), so the page names a state as the watch does. What a gesture does
// is not here: it comes from facts.gesture_map (nav.c), see matrix.js and lab.js.

/**
 * One state of the ring.
 * @typedef {{ id: string, kind: string, palette: string, label: string, fact: string[] }} StateRow
 */

/** `kind` keys facts.ring.kinds, `palette` keys facts.palette, `fact` is the path in facts.states. */
export const STATE_ROWS = [
  { id: 'work', kind: 'work', palette: 'working', label: 'Working', fact: ['work'] },
  { id: 'ask', kind: 'ask', palette: 'ask', label: 'Has a question', fact: ['input', '1'] },
  {
    id: 'perm',
    kind: 'perm',
    palette: 'permission',
    label: 'Needs approval',
    fact: ['input', '0'],
  },
  { id: 'done', kind: 'done', palette: 'done', label: 'Done', fact: ['done'] },
  { id: 'error', kind: 'error', palette: 'error', label: 'Error', fact: ['error'] },
  { id: 'idle', kind: 'idle', palette: 'idle', label: 'Idle', fact: ['idle'] },
];

/** The firmware's name for a row's state (facts.states), else the built-in one. */
export function stateLabel(row, facts) {
  const [state, sub] = row.fact;
  const s = facts.states?.[state];
  return (sub ? s?.sub_labels?.[sub] : s?.label) ?? row.label;
}

/** Whether a tap on the centre opens the request (ASK) rather than the agent menu (nav.c). */
export const askable = (row) => row.id === 'perm' || row.id === 'ask';

export const rowColor = (row, facts) => facts.palette[row.palette];
