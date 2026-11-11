# UI profile: the watch as it stood before v3

Audit date 2026-11-10, `main` at `ff33ff3` (UI v2 "Deck"). Method: every simulator script that takes
a screenshot was run headless (56 frames across HOME, AGENT, ASK, WATCH, SCROLL, STATUS, VOICE and
KEYS), read side by side, and checked against `UI-V2.md`, `nav.c` (the gesture table) and the
theme tokens. This file is the "before"; [UI-V3.md](UI-V3.md) is the answer to it.

## 1. What the product is

A 466 × 466 round AMOLED, two side buttons (A lower left, B lower right), a touch panel, a vibration
motor, a speaker and a microphone. It shows the state of several coding agents, buzzes when one needs
you, and lets you answer: bring the terminal forward, approve or deny a permission, pick an answer,
dictate or type a reply, watch live steps, scroll the terminal.

Eight modes: HOME (one session card plus an index ring), AGENT (actions for one session), ASK (a
permission or question), WATCH (live steps), SCROLL (terminal scrollback), STATUS (link, battery,
quiet, sounds), VOICE (dictation), KEYS (swipe keyboard).

## 2. What already works and must be kept

- **The model is right.** One session per glance, a ring that indexes all of them, lists you move
  through with detents. Nothing in v3 changes this.
- **Safety is structural.** A tap never approves; approve is a 1.2 s hold bound to the request id,
  armed 300 ms after the card appears; deny is a hold or a strict long swipe; the first touch after
  a glow only reveals; approve, deny and reject differ by haptic count. These live in `nav.c`, not in
  the drawing code.
- **The layering is right.** Interaction (`nav.c`), plumbing (`ui.c`) and presentation (`ui_*.c`,
  `ui_theme`) are separate, so a reskin does not touch a safety rule.
- **Colour-blind-safe state colours** (Okabe-Ito), and state is never colour alone (word plus glyph).
- **The redraw discipline**: change-only setters and a pixel budget enforced by tests.

## 3. Findings

Severity: **A** hurts comprehension or trust, **B** hurts polish, **C** is a nit.

### 3.1 Hierarchy and density

| # | Sev | Finding | Seen in |
|---|---|---|---|
| H1 | A | HOME stacks eight rows in one weight: agent · branch, repo, state, summary, time, action pill, key badges, link row. Nothing wins the glance. | `deck_work`, `deck_long_work` |
| H2 | A | The action pill (`FOCUS`) says the same thing as the `A FOCUS` badge 30 px below it, and looks like a button although it cannot be tapped. | every `deck_*` |
| H3 | B | The link row (`PC · dev-mac · 80%`) is permanent, green, and reports that nothing is wrong. Status that needs no action is shown as loudly as status that does. | every `deck_*` |
| H4 | B | Elapsed time has its own row with dead space above it when the summary is one line. | `ring_error`, `ring_idle` |
| H5 | B | One typeface in one weight (Montserrat Regular). Size is the only way to rank text, so sizes multiply (16/20/24/28/32/48). | all |
| H6 | B | Muted text is `#8b949e` at 16 px on black: legible in the simulator, weak on a wrist. | headers, hints |

### 3.2 The ring means six things

| # | Sev | Finding | Seen in |
|---|---|---|---|
| R1 | A | The rim is the session index on HOME, the list of menu items in AGENT, the list of answers in ASK (blue, orange and vermillion arcs at once), the hold progress, the scroll position and the microphone level. The same capsules mean "agents" on one screen and "menu rows" on the next. | `agent_wheel` vs `deck_work`, `ask_permission` |
| R2 | B | Three shape encodings (dots for error, twin tracks for permission, capsules for the rest) plus a white inner underline for selection. The dotted error arc reads as a loading spinner; the underline reads as a rendering artefact. | `deck_error`, `selftest_ring`, `ring_idle` |
| R3 | B | Unselected segments keep 70 % brightness, so a five-session ring is five competing colours around a card that is trying to be calm. | `deck_mixed`, `ring_ask` |

### 3.3 The interface explains itself in sentences

| # | Sev | Finding | Seen in |
|---|---|---|---|
| X1 | A | Six screens carry a permanent instruction line: `long swipe down to go back`, `long swipe down to deny`, `swipe up to go back`, `drag like a trackpad`, `tap to stop`, `hold to send`, `drag to scroll · hold to go back`. A control that needs a caption is not yet a control. | AGENT, ASK, STATUS, SCROLL, VOICE, WATCH |
| X2 | A | **"Back" is a different gesture on every screen**: long swipe down (AGENT, VOICE, a question in ASK), swipe up (STATUS), hold still 600 ms (WATCH), tap (SCROLL), double tap below the keys (KEYS). Only B-held is universal, and it is not shown. | `nav.c` |
| X3 | A | In a list, tapping a row does not pick that row: any centre tap commits whichever row is selected. The row you touch is not the row you get. | AGENT, ASK, STATUS |
| X4 | B | Hold-to-confirm is announced by a small `HOLD` tag at the right edge, which collides with the rim. Nothing on the item itself looks holdable, and the progress appears on the ring, away from the finger. | `ask_permission`, `agent_interrupt` |
| X5 | B | Chevrons on both sides of the selected row (`> Approve <`) add two marks that carry no information. | every wheel |
| X6 | B | Key legends are full words in the state colour on every screen (`A OPEN ON PC`, `B NEXT`), as loud as content. | all |

### 3.4 Layout on a round display

| # | Sev | Finding | Seen in |
|---|---|---|---|
| L1 | A | Text runs into the ring: the ASK header sits on the selection underline, three-line prompts are clipped by the rim (`What should the commit message`), the `HOLD` tag overlaps the ring. No shared safe-area rule. | `approve_card`, `ask_dictate`, `ask_error` |
| L2 | B | KEYS: `q`/`p` and `1`/`0` touch the rim and the candidate row is cut by it. (Owned by issue 1e57; reported there.) | `keys_cands`, `keys_symbols` |
| L3 | B | Vertical rhythm differs per screen: headers at y 83, 99, 107 and 113; wheels centred at 233 or 272; toasts in two slots. | all |
| L4 | C | A horizontal rule separates the prompt from the options in ASK; space would do. | `ask_*` |

### 3.5 Language

| # | Sev | Finding |
|---|---|---|
| W1 | B | Internal words on glass: `FOCUS`, `FOCUS + ACK`, `0 steps`, `Privacy: preview`, `Firmware 0.1.0-sim` as a selectable row. |
| W2 | B | Three names for one action: `FOCUS` (HOME), `Open on PC` (ASK), "focus the terminal" (docs). |
| W3 | B | State words mix parts of speech: `Working`, `Question`, `Approve?`, `Done`, `Error`, `Idle`. |
| W4 | C | "Watch" as a menu item on a watch. |
| W5 | C | SCROLL reads `-142 lines / above the bottom`: a number with a sign and a two-line unit. |

### 3.6 Glyphs, colour, motion

| # | Sev | Finding |
|---|---|---|
| G1 | B | State glyphs are LVGL's built-in Font Awesome subset (refresh, bell, warning, pause) and a typed `?`. They do not share a weight or a shape with each other or with the type. |
| G2 | B | Up to four hues on screen at once (ASK: sky, orange, vermillion, plus white). Colour stops meaning "this needs you". |
| G3 | B | No transitions: modes replace each other in one frame, so nothing tells you whether you went deeper, sideways or back. |
| G4 | C | Toasts take the state tint at 60 %: a brown, a teal and a navy pill for the same component. |

### 3.7 Missing

| # | Sev | Finding |
|---|---|---|
| M1 | B | A watch-shaped device never shows the time (the clock exists: STATUS prints `Quiet until 12:30`). The empty deck is `No sessions / Waiting for an agent / swipe down for status`. |
| M2 | B | STATUS mixes two controls (Quiet, Sounds) with three read-only rows in one wheel; the read-only rows can be selected and do nothing. |
| M3 | C | After an answer the ASK screen is a single coloured sentence and a second pill repeating it (`Denied` twice). |

## 4. Diagnosis

The interaction model is sound; the presentation has grown by addition. Each feature brought its own
row, its own hint and its own use of the ring, and every one was reasonable alone. Together they
produce a screen that is correct but must be read rather than glanced at. v3 therefore changes
little about *what* the watch does and a great deal about how much it shows:

1. One purpose per element. The ring is the index of agents and nothing else; progress happens on
   the thing being held.
2. Show status by exception. A healthy link, a full battery and a live terminal are silence.
3. One grammar. The same gesture goes back everywhere; the row you touch is the row you get.
4. Rank with weight, not size. Two weights, five sizes, one accent colour per screen.
5. Keep every safety rule exactly as it is.

## 5. Outside the UI (found during the audit)

- The simulator suite takes about 7.5 minutes because 60-odd scripts replay in real time, one after
  another (`ctest` 463 s). They are independent processes.
- The Atlas tests fail on Node 18 (issue f95e, already filed).
