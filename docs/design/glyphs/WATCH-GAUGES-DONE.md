---
status: implemented
owner: ui
issue: 20261114-8c3d
last_reviewed: 2026-11-14
summary: Sigil WATCH, the gauges and the done row as built
---

# Sigil: WATCH, the gauges and the done row, as built

Part of [GLYPHS.md](../GLYPHS.md) §5 (issue `20261114-8c3d`). It describes three rows of that table as they are on
glass: **WATCH** (`ui_watch.c`), **GAUGES** (`ui_gauges.c`, `gauges.c`) and **DONE** (`ui_done.c`, `done.c`).
Renders: `simulator/scripts/sigil_watch.txt`, `sigil_gauges.txt`, and the older `watch*.txt`, `gauges_*.txt`,
`done_*.txt`.

Every word these screens used to draw is still in `ui_report()`, in the same lines (rule 9). The scripts'
`expect_label` lines did not change.

## 1. WATCH: a lane of glyph nodes

The step timeline is drawn like GRAPH ([GRAPH.md](../GRAPH.md) §4), turned on its side and enlarged. Time runs
left to right.

- **Who:** the project's sigil at the top (r 22, y 50) and the agent's mark under it, as on HOME.
- **The lane** (y 202) crosses the screen while the session works.
  - The current step is the **hero node** at the centre: its tool as a 96 px glyph in a ring of r 72.
    - running: a dim ring with a bright comet going round it (one turn in 2 s);
    - returned: a filled disc in the session's colour, the glyph in black;
    - failed: a filled red disc.
  - The three steps before it are smaller discs to its left (r 22, 17, 13), dimmer the older they are. A failed
    one stays red.
  - Where older steps were dropped, the lane fades out towards the bezel.
  - To the right, the lane runs into the **step count** in a badge (a count: rule 4), then three fading dashes.
- **Under the lane:**
  - the time in this state as a tally of minutes (§3.3: there is no room for a circle beside the nodes);
  - the selected step's **argument** on two rows. It is the one piece of text on the screen (rule 1: the agent's
    step summary).
- **Selecting a step:** a tap on an earlier node selects it (a white ring) and shows its argument. A tap on the
  hero, or a new step, selects the current step again. The report marks the selected row with `> `.
- **Not working:** the lane gives way to the state as a 128 px glyph (y 206) with the time in that state as dots
  round it (r 88), and the done row (§3) under it. A tap on the glyph opens ASK when the session waits for you,
  as before; the report still says `Answer`.
- **Keys:** A is the desktop (it brings the terminal to the front), B the next session, as on HOME.

### A step as a glyph

The tool's name is matched without case against the kinds of the agent graph (`graph.rs`, `kind_of`). A
sub-agent's step leads with the agent (`explore: bash`): the tool is what follows.

| Tool | Glyph |
|---|---|
| `Read`, `View`, `read_file`, `NotebookRead` | eye |
| `Edit`, `Write`, `Create`, `MultiEdit`, `NotebookEdit`, `apply_patch` | pencil |
| `Bash`, `PowerShell` and the other shell tools | terminal; git-commit when the command starts with `git` or `gh`; flask when it is a test run |
| `Grep`, `Glob`, `LS`, `LSP`, `code_search` | magnifying glass |
| `WebFetch`, `WebSearch` | globe |
| `Task`, `Agent` and the agent tools | robot |
| `AskUserQuestion`, `ask_user` | the question glyph |
| `TodoWrite`, the plan tools | list-checks |
| an MCP tool (`mcp__server__tool`, `server-tool`) | plug, and the whole step text below |
| anything else, or prose | the working glyph (the hero) or a dot (an earlier node), and the whole text below |

A tool that has a glyph is not spelled out: only its argument is text. Any other step keeps its whole text, so no
tool name is lost.

### What changed for the user

- The time in the working state was `1:35`; it is now a tally, to the minute.
- The arguments of the earlier steps were three rows of small text; they are now one tap away each.
- Nothing else: every gesture of UI-V2 §4.7 is as it was (rim and B move through the sessions, A and a centre tap
  focus, a drag scrolls the terminal, a still finger or B held goes back).

## 2. Gauges: arcs inside the bezel

### 2.1 Context (the session card)

An arc along the bottom of the bezel, between the two key hints: r 196, 6 px wide, 30 degrees either side of
6 o'clock. It fills from the left. Grey while there is room, amber from 70 %, vermillion from 90 %; unknown draws
nothing.

It keeps this place whatever holds the bottom slot (the graph strip, the book disc): it is below
both. HOME still moves the parent it gives the gauge (`CTX_Y`, `CTX_Y_LOW` in `ui_home_card.c`); the arc undoes
that move, so those two constants no longer matter.

### 2.2 Plan usage (the empty and ambient faces, STATUS)

A pair of arcs that start at 6 o'clock, each over its 20 px glyph (the key hints are on the upper rim, issue
20261125-d8b5):

- the **5 hours** (a clock) run up the left side, the **week** (a calendar) up the right;
- r 194, 6 px wide, from 11 to 118 degrees from 6 o'clock on each side.

One arc is two scales at once:

- **The fill is the usage.** White while there is room, amber from 80 %, vermillion from 95 %. Known and 0 % is a
  dot at the start.
- **The bead is the clock:** how far through the window the time is. The arc beyond the bead is the time left
  until the reset (it was `resets in 2h 10m`). A fill that has passed the bead is ahead of the clock.
- **The ring** marks the moment the window runs out at the current pace, when that is before the reset (it was
  `runs out 14:30`), on the same scale.
- An unknown, stale or reset window is a bare track, never 0 %.

The faces show the pair while the 5-hour window is known, at 45 % on the ambient face. STATUS adds the two figures
(`42%`, `18%`) on one row under its clock, each on the side of its arc; a figure is amber when the window is on
pace to run out, and a dash when it is unknown.

For the screen that hosts it: the arcs and their beads stay inside r 187 to 201 and above y 330; the glyphs are at
y 39; the figures are centred on y 166.

### 2.3 Model, mode and pull request (AGENT)

One row, centred on (233, 108) of the parent AGENT gives, 32 px high:

- the **model's name** as text: it is content from the agent and has no picture;
- the **permission mode** as a glyph, only when it is not the default:

  | Mode | Glyph | Colour |
  |---|---|---|
  | Plan | map | white |
  | Accept edits | pencil | amber |
  | Auto | fast-forward | amber |
  | Don't ask | chat-circle-slash | amber |
  | Bypass | shield-slash | vermillion |

- the **pull request** as the pull-request glyph and its number, in the colour of its review state: in review
  yellow, approved green, changes requested vermillion, draft grey.

## 3. The done row

What a finished turn did, as one row of marks (56 px high, up to 340 px wide), on the session card in place of
the stale step text and on WATCH under the state glyph.

| Part | Mark |
|---|---|
| Files changed | the files glyph with the count in a badge at its corner |
| Lines | a two-colour bar: green for added, vermillion for removed, split in their proportion. Its length steps with the size of the change: 24 px under ten lines, 12 px more for each digit, up to 72. A change of no lines is a short grey bar |
| Tests | the flask with a tick in a green dot; when they failed, the flask in the error colour with a cross in a red dot |
| Turn time | the timer glyph and the minutes as a tally; under a minute, the glyph alone |

`done.c` still decides which parts the line of words keeps (the report prints that line). When the marks do not
fit, the row asks it for less: the lines go first, then the time, then the files, never the tests. While the link
is down the row is grey, like the rest of the card.

## 4. New glyphs

| Meaning | Glyph | Define |
|---|---|---|
| How long a turn took | timer | `ICON_TIMER` |
| The plan's 5-hour window | clock | `ICON_CLOCK` |
| The "don't ask" permission mode | chat-circle-slash | `ICON_DONT_ASK` |

The 96 px icon font also holds the eleven node glyphs of GRAPH now (`NODE96` in `gen.mjs`), for WATCH's hero.

## 5. Text that is still drawn

- WATCH: the selected step's argument, or the whole step text when the tool has no glyph (rule 1: the agent's
  step summary); the step count in its badge (rule 4: a count in a badge).
- STATUS: the two usage figures (rule 4: a percentage a gauge already shows).
- AGENT: the model's name (content from the agent); the pull request's number (a number that cannot be drawn).
- The done row: the count of files in its badge (rule 4).
