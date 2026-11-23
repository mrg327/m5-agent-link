---
status: implemented
owner: ui
issue: 20261114-8c3d
last_reviewed: 2026-11-14
summary: Sigil ASK screen as built
---

# ASK in Sigil

Status: **built in the simulator** on branch `glyphs-ask` (issue `20261114-8c3d`); not on the device.
The design system is [GLYPHS.md](../GLYPHS.md). The functions, gestures and safety rules are those of
[UI-V3.md](../UI-V3.md) §4.3 and §1, [ANSWER-WINDOW.md](../ANSWER-WINDOW.md),
[STANDING-RULES.md](../STANDING-RULES.md) and [LAST-MESSAGE.md](../LAST-MESSAGE.md) §3. Where one of those
documents says how ASK *looks*, this one wins; what ASK *does* is unchanged.

Files: `firmware/components/core/src/ui_ask.c`, `ui_countdown.c`. Renders: `simulator/scripts/sigil_ask.txt`.

## 1. The screen, top to bottom

- **Who asks.** The project's sigil (r 22 at y 50, as on HOME), the agent's mark right of it, a globe
  left of it for a remote session. No names.
- **What is asked** (content, so text: rule 1), centred as a block in y 72..176:
  - the title in grey: the request's own title, else the tool, or a question's header. With none of
    them the row is left out (the report still says `Permission request` / `Question`);
  - the text in white: a permission's text is measured at 292 px as before, a question's at 312 px.
    One or two short lines are set large (24 px); otherwise up to three lines at 20 px;
  - a text that goes on (more than three lines, cut by the daemon, or a question whose options have
    descriptions) shows two lines and the **book** glyph in the request's colour. A tap on the block
    opens the reader.
- **What can be done**: discs (§2) or rows (§3).
- **Keys.** A's hint is the desktop: it *is* Open on PC (a tap on it does the same, and it is ringed in
  white while that item is selected). B's hint is the caret (next item), hidden with one item.
- **Position.** Pips at y 404: which question of several; in the reader, which page.
- **The answer window** is the arc at the bezel (§6).

One item is always selected. Button B, the dial (a rim drag) and a vertical drag step through the
items; a tap on another item selects it. **Only the selected item is lit in its colour and only it can
be held.** A selected hold item shows the path of its hold ring, dim, round its disc; a tap item shows
none. A hold fills that ring in white (and the index ring, in the item's colour, as before).

## 2. Discs: a request without options of its own

A bare permission, or a question the watch can only dictate to or hand back.

| Item | Where | Size | Glyph, colour when selected |
|---|---|---|---|
| the first item (Approve; Dictate; Open on PC when it is alone) | (304, 262); (233, 262) with no hold item beside it | r 74 | tick, orange; microphone, yellow; desktop, grey |
| Deny | (140, 262) | r 52 | cross, vermillion |
| Deny and Always allow together | (146, 218) and (146, 306) | r 36 | cross; double tick, orange |
| a tap item that is not first (Dictate) | (233, 378), between the key hints | r 26 | microphone, yellow |
| Open on PC | button A's hint (101, 365) | r 20 | desktop; ringed when selected |

Unselected discs are grey with a light glyph. A grant that is still shut (the text has not been read,
or the rule does not fit) is dim: it wakes in its colour when the last page has been on screen.

## 3. Rows: a request with options in words

The agent's own choices and a question's options are content, so they stay text. They stand down a
spine of discs, the selected one in the middle (y 262, a band 72 px tall: the hold target), one
neighbour above and one below (y 202 and 322). The list has a top and a bottom on the glass (no
wrap-around neighbour); stepping still wraps. The spine is placed once per list so that the widest
option, with its disc, is centred.

- The selected row: a disc of r 30 with what the item does (tick: choose or grant; cross: an option
  that denies; microphone: Other / Dictate; paper plane: Send; caret: Next; desktop: Open on PC), in
  the item's colour, and the option's words in white, large on one line or 20 px on two.
- A neighbour: a thin ring of r 14 and its words in grey. The ring is empty for a plain option, holds
  the tick or cross of a permission's option, and the glyph of a glyph item.
- Multi-select: a ticked option's disc is yellow with the tick (a yellow ring with the tick as a
  neighbour); an unticked one is an empty grey disc (an empty ring).

## 4. Always allow

While the double-tick item is selected, the rule stands where the request was, exactly as it will be
saved (never an ellipsis; large when all of it fits large), under two glyphs:

| Glyph | Meaning (the report's words) |
|---|---|
| folder | `Saved for this project` |
| hourglass | `Until this session ends` |
| asterisk, in the request's colour | `Wide: * matches anything` |
| equals sign, grey | `Exactly as written` |

A rule that needs more than two lines is not shown in part: the sign *double tick, prohibit, desktop*
stands in its place (`This rule is too long to show on the watch` · `Answer it on your computer`) and
the item is shut.

## 5. The reader

The title stays; the text fills a window 320 px wide and seven lines tall from y 104 (a single short
page is centred in it). Pips at the bottom say which page; a small working glyph under the text says
that more is still arriving. B's hint is the caret-down (more), then the tick (done). Gestures and the
lock are those of UI-V3 §4.3a.

## 6. The countdown (`ui_countdown.c`)

One `ui_arc` in the band r 213..216: a whole circle when the window opens, its end running back to
12 o'clock as the time runs out (the way a hold ring fills from there). Grey; white in the last 10 s.
Shown on ASK and on the request's card on the deck, hidden once an answer is out.

## 7. Results

One hero glyph (128 px at y 206) in the result's colour; under it only content and where the request
is now.

| State (the report's words) | Hero | Under it |
|---|---|---|
| `Approved` · `Waiting for your computer` | tick, green | desktop |
| `Always allowed` · the rule · the scope | double tick, green | the rule's first line (text), then the folder or the hourglass |
| `Denied` | cross, vermillion | desktop |
| `Sent` · `"Vue"` | paper plane, yellow | the option in the agent's words (text), desktop |
| `Sent` · `2 answers` | paper plane | a tick for each answer |
| `Sent` · `Your own words` | paper plane | microphone, desktop |
| `On your computer` (handed back) | desktop, grey | nothing |
| `On your computer` (the time ran out) | desktop, grey | hourglass |

The timings are unchanged: an answered state stays at least 900 ms (2.5 s for a saved rule); the
on-your-computer state 3 s, and any input leaves it.

## 8. Signs (toasts)

A tap on a hold item never commits it. Its ring starts and falls back (a nudge, 0.6 s) and the sign
says *finger, and what the hold does*; a shut grant is told why. The words are unchanged in the report
and the log; the vocabulary (`ui_theme.c`) draws them:

| Words | Sign |
|---|---|
| `Hold to approve` / `Hold to choose` | finger, tick |
| `Hold to deny` | finger, cross |
| `Hold to always allow` | finger, double tick |
| `Hold to send` / `Hold for the next question` | finger, paper plane / finger, caret |
| `Read it first` | book |
| `Too long to approve here` | desktop, tick |
| `Rule too long for the watch` | double tick, prohibit |
| `Tick at least one` | list with ticks |

## 9. Text that is still drawn, and why rule 1 allows it

- The request's title and text, a question's header and text: what the agent asks.
- The agent's own options and a question's options: what the agent asks.
- The standing rule: what the agent asks to have saved.
- The reader's text: the same request, whole.
- In a result: the option that was sent and the rule that was saved.

Everything else (the agent and project names, `1 of 2`, `Read all`, `2 of 3 · loading`, the scope and
breadth captions, every item's name, every hint, every result word) is a glyph, a disc, a ring, an arc
or a pip, and is said in words only in `ui_report()`.

## 10. What changed outside the two files

- `ui_kit.c` / `ui_kit.h`: `ui_disc_set_track()` (the dim path of a hold ring; off by default).
- `ui.c`: a tap on one of ASK's items counts as a centre tap (`ui_ask_target_hit()`), as for HOME's
  book disc: button A's hint and the disc between the hints lie beyond the centre zone. Presses do
  not: every hold target is inside r 150.
- `ui_theme.c`: the vocabulary rows of §8.
- `ui_icons.h`, `gen.mjs`, the icon fonts: `ICON_WILD` (asterisk, Bold), `ICON_EXACT` (equals, Bold),
  `ICON_FOLDER` (folder).
- Scripts: only tap, hold and press coordinates moved (the Deny disc, the Always-allow disc, Open on
  PC on A's hint, a neighbour row); no assertion was removed. `ask_second_tap.txt` gained a case.
  New: `sigil_ask.txt`.
