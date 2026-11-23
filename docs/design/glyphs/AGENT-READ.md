---
status: implemented
owner: ui
issue: 20261114-8c3d
last_reviewed: 2026-11-14
summary: Sigil AGENT and READ screens as built
---

# Sigil: AGENT and READ as built

Part of [GLYPHS.md](../GLYPHS.md) (§5, the rows AGENT and READ). Files: `firmware/components/core/src/ui_agent.c`,
`ui_read.c`. Renders of every state: `simulator/scripts/sigil_agent.txt` (`sigil_agent_*.png`, `sigil_reply_*.png`,
`sigil_read_*.png`).

Both screens are assembled from the primitives of `ui_kit.h`. The functions and gestures are the ones of
[UI-V3.md](../UI-V3.md) §4.2 and [LAST-MESSAGE.md](../LAST-MESSAGE.md) §1; the report lines are the same words as
before (rule 9).

## 1. The arch

A menu on these screens is an orbit that is open at the bottom (`ui_orbit_set_arch(o, 222, 50)`):

- The discs sit on r 150 about the screen centre, from 249 degrees over 12 o'clock to 111 degrees. The first item
  is at the left end; the rest follow clockwise. Up to five discs are 50 degrees apart and sit together at the
  top; six and seven spread over the whole arc.
- The arc ends 24 degrees short of each key hint. So a selected disc (r 40) with its hold ring never touches a
  hint, and the two hints read as the last two marks of the circle.
- The bottom is free: the slot between the key hints holds a row of content, and the toast's sign.
- The discs reach past the centre zone (r 150). `ui.c` lets a tap, a press and a long press on a disc through as
  a centre touch (`ui_agent_hit`, `ui_read_hit`). A slide along the bezel still dials.

## 2. AGENT: the session menu

| Where | What | Words in the report |
|---|---|---|
| Arch | One disc for each action. The selected one is r 40 in the session's colour; Stop is red, Run again blue | `> Activity`, the next four items, `N items` |
| Centre (y 212) | The session's sigil, r 56, white. No folder: the dashed-folder glyph | |
| y 298 | The project's name (rule 2: the one place it is spelled out). A remote session has the globe before it | `webapp`, `HOST · webapp` |
| y 330 | The gauges' readout, drawn by `ui_gauges.c` in a window 208 px wide | as `ui_gauges.c` reports it |
| y 368, between the key hints | The message that waits to be typed into the session, after the queue glyph (the user's own words: rule 1) | `Queued: …` |
| Key hints | A: tick (run the selection). B: caret (the next one), hidden when there is one item | |

The glyphs of the actions:

| Action | Glyph |
|---|---|
| Activity / Graph / Read | pulse / git-fork / book-open |
| Reply, Reply now | arrow-bend-up-left |
| Notify when done / Don't notify | bell-ringing / bell-slash |
| Snooze 30 min / Unsnooze | moon / sun |
| Open on PC | desktop |
| Dismiss | cross |
| Cancel queued | backspace (it rubs out the message that waits) |
| Stop / Run again (hold) | stop-circle / arrow-clockwise |

Gestures, unchanged in meaning:

- The dial (a slide along the bezel), a vertical drag in the centre and button B step the selection; it wraps.
- A tap on a disc runs that disc. A tap anywhere else in the centre, and button A, run the selection.
- Stop and Run again run only by a 1.2 s hold that starts on their own selected disc, not sooner than 300 ms
  after the menu appeared. The hold fills the disc's ring, and the bezel, because the finger covers the disc.
  Moving the selection, dragging or sliding ends the hold.
- A tap on Stop or Run again selects it and sends nothing. A sign shows (hand-tap and the action's glyph; in
  words `Hold to stop`, `Hold to run again`) and a fifth of the disc's ring shows for 0.7 s: the ring to fill.
- A finger held still anywhere off the discs goes back. So do swipe right and button B held. The menu closes by
  itself after 8 s without input.

## 3. AGENT: the reply list

Reply (and Reply now, for a working session) turns the screen into the list of quick replies.

- **Top:** the reply glyph in yellow beside the session's sigil (r 22): "reply to this one". In words:
  `Reply to webapp`.
- **The agent's closing question** under them, two lines (what the agent asks: rule 1). Not when queueing.
- **Rows of text** (rule 1: the session's own answers first, then the user's quick replies). Three show: the
  selected one in the middle, its neighbours above and below, dim; the list wraps. The selected reply is drawn
  at 34 px when it fits 300 px, else at 24 px, between two yellow arcs at 9 and 3 o'clock.
- **Page dots** under the rows: one for each item, the selected one larger.
- **Dictate and Keyboard** are discs between the key hints (microphone, keyboard). They are the last items of the
  list; the selected one is yellow. With no quick reply at all they are large and in the middle.
- **Key hints:** A is the paper-plane on a row (it sends that reply) and the tick on a disc; B is the caret.
- A tap on a row sends that row; a tap on a disc opens VOICE or KEYS; a tap elsewhere and button A run the
  selection. Nothing here is a hold: the daemon types the reply by its index, never text from the watch.

## 4. READ: the last message

**The reader**

- The session's sigil (r 22) at the top and the agent's mark under it, as on HOME. In words: `Claude Code · webapp`,
  `Last message`.
- The message, seven lines of 20 px text, 320 px wide, left-aligned (a summary: rule 1). The page geometry is the
  one of the legibility pass, so page counts did not change.
- On the key-hint row, between the hints: the page dots (`Reading 2 of 3`). Beside them a small ring that fills as
  the rest of the message arrives (`Loading`), or the error glyph in red when it did not (`Not loaded`).
- Key hints: A is the desktop (open on the computer, as on HOME). B is the caret-down while there is more, the
  reply arrow on the last page.
- A tap, button B and a drag up turn the page; a drag down goes back one. Past the last page (all of it here)
  comes the end.

**The end**

- An arch of discs over the session's sigil (r 40): Dictate (microphone) and Reply (reply arrow), yellow when
  selected; Read again (book) and Open on PC (desktop), the session's colour when selected. In words: `Answer it`,
  `> Dictate`, `Reply`, `Read again`, `Open on PC`.
- Under the sigil the last lines of the message, three lines, grey: what is being answered.
- A tap on a disc runs it; a tap elsewhere runs the selection; button B and the dial step; button A opens the
  session on the computer. Swipe right, button B held and a finger held still off the discs go back to the last
  page.

## 5. Text that is still drawn, and why rule 1 allows it

| Text | Why |
|---|---|
| The project's name under the sigil (AGENT menu) | Rule 2: the one place a mark is spelled out |
| The queued message (AGENT menu) | The user's own words (dictated or a quick reply) |
| The agent's closing question (reply list) | What the agent asks |
| The reply rows (reply list) | The agent's own answers, and the user's quick replies: text entry |
| The message and its last lines (READ) | A summary: the agent's last message |
| The gauges' readout (AGENT menu) | Not this screen's: `ui_gauges.c` draws it, and GLYPHS.md §5 GAUGES turns it into glyphs |

## 6. What changed outside the two files

- `ui_kit.c`, `ui_kit.h`: `ui_orbit_set_arch()`. The whole circle stays the default.
- `ui.c`: a touch on a disc of AGENT or READ past r 150 is a centre touch; the toast blanks the slot between the
  key hints on AGENT and READ too.
- `ui_theme.c`: two rows of the vocabulary, `Hold to stop` and `Hold to run again`.
- `simulator/src/sim_script.c`: `session SID STATE -` makes a session with no folder.
- Scripts: coordinates moved to the discs in `agent_row_tap`, `agent_hold_neighbour_row`, `agent_tall_row_in`,
  `agent_tall_row_out`, `agent_hold_cancel_on_select`, `quick_replies`, `jobs_menu`, `jobs_rerun`,
  `last_message_reply`. No assertion was removed.
