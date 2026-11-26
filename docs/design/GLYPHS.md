---
status: implemented
owner: ui
issue: 20261114-8c3d
last_reviewed: 2026-11-26
summary: '"Sigil": the glyph-first design system'
---

# m5-agent-link: Sigil, the glyph-first watch UI

Status: **design system, being built** on branch `glyphs` (issue `20261114-8c3d`). It replaces the
look of [UI-V3.md](UI-V3.md) on every screen. The functions, gestures and safety rules of UI-V3 stay.

The user's brief (2026-11-14): the watch must look strikingly different from the current UI,
"borderline alien". Show information as images, glyphs and shapes that take a real share of the
screen. Trust the user to learn the symbols. Text belongs in three places only: **dictation, text
entry and summaries**. Direction picked from three mock-ups: **B, Sigil**.

A first attempt swapped words for glyphs inside the UI-V3 layout (commit 23180d3). The user judged it
identical to the old UI. So a screen is rebuilt from nothing under the rules below; it is not edited
word by word.

## 1. Rules

1. **Text is content someone wrote, and nothing else.** Allowed:
   - a dictation transcript, and text being typed (keys, candidates);
   - a summary: the agent's step summary, its last message, a done summary;
   - what the agent asks: the request text on an approval, a question and its options, a standing
     rule. The user has to read these to decide.

   Everything else the watch used to say in words is a glyph, a shape, a colour or a position:
   state names, action names, legends, hints, captions, confirmations, errors.
2. **Identity is a sigil.** A project is a generated mark (§3.1), not its name. The same name always
   gives the same mark. The name is shown in one place only: under the sigil in the session menu
   (§5 AGENT), so a mark can be learned. Agent kind is a small glyph; a branch is not shown on HOME.
3. **State is one giant glyph.** The state of the thing on screen is a single glyph of about 128 px
   in the state colour, at the centre.
4. **Quantities are geometry.**
   - Elapsed time: dots, one a minute, every tenth larger (§3.3).
   - A countdown: an arc that drains.
   - Progress, volume, battery, context, plan usage: arcs or filled shapes.
   - Digits are allowed for the clock, a count in a badge, and a value that cannot be drawn (a
     percentage a gauge already shows may add its figure). Never a unit word.
5. **Actions are discs.** An action is a glyph in a filled circle.
   - The primary action is a large disc in the state colour (radius about 78 px); its hold fills a
     ring around it.
   - Secondary actions are smaller grey discs.
   - A menu is an orbit of discs around its subject (§3.5).
   - No rows of labels, no pills with words.
6. **Button hints sit at the buttons.** What A and B do is a glyph in a thin ring at the rim, next to
   the physical button: A upper left in amber, B upper right in blue, as the StopWatch's keys sit on its upper
   rim when it is upright with USB at the bottom (issue 20261125-d8b5). No letters. Where the key is
   safe (HOME, WATCH) a tap on the hint presses it (§3.6).
7. **A result is a burst.** Approved, denied, sent, handed back to the computer: one hero glyph
   filling the centre, in its colour, then the screen moves on. No words.
8. **An error is a sign.** One or two glyphs side by side ("computer, refused"). A rare diagnostic
   that has no picture (an update that failed its checksum) is the error glyph and a short number;
   the words go to the log and the report.
9. **The words live in the report.** Every screen still names what it shows, in words, in
   `ui_report()`: `Working`, `> Approve HOLD`, `Offline · 1s`. The debug log, `agentlink` and the
   simulator tests read that. It is the legend for every glyph, and the tests keep asserting meaning.
10. **Nothing is lost, nothing gets less safe.**
    - Every function and gesture of the screen being rebuilt still exists.
    - The safety invariants of UI-V3 §1 are not negotiable: the approval shows the request text; a
      tap never approves or denies; an approve-class commit is a 1.2 s hold bound to the request id,
      armed 300 ms after the card appears; deny is the 0.8 s hold; the first touch during a glow only
      reveals; haptics differ by count; button A never approves.
    - The redraw discipline of UI-V2 §8a stays: change-only setters, no full-screen redraw on a tick.

## 2. Colour

Unchanged tokens (`ui_theme.h`): working blue, question yellow, approval orange, done green, error
vermillion, idle grey; black background. Colour still means state and nothing else. Button A's hint is
amber and B's is blue because the physical buttons are (yellow and blue).

## 3. Primitives (`ui_kit.h`, built first, used by every screen)

A screen is assembled from these. A screen that needs something new adds it here, not locally.
`firmware/components/core/src/ui_kit.h` is the reference; this section is its summary.

Common to all of them:

- Each is one LVGL object that draws itself. Positions are **centres**, in the parent's coordinates. Angles
  are degrees clockwise from 12 o'clock.
- Every setter is change-only. Calling it with the value it already has costs nothing; a change invalidates
  only what moved (one dot, the piece of an arc between the old end and the new). They are safe to call
  from a tick.
- None takes input. A hit test is a function the screen calls from its own gesture handler.
- A glyph argument is an `ICON_*` literal (`ui_icons.h`); the object keeps the pointer, not a copy.
- `kit N` in a simulator script shows page N of a gallery of every primitive (`simulator/scripts/kit_gallery.txt`).

### 3.1 Sigil: `ui_sigil_*`

Three concentric rings of arcs on an 8-sector grid and a core, all derived from the first 10 bytes of the
SHA-256 of the name (the port of `Scr.sigil()` in `docs/design/glyphs/mock.py`: per ring, one hash byte
picks the sectors, two more force at least two; runs of neighbouring sectors merge into one arc; the core
is a dot or a bar). Round caps. An empty name (no folder) draws the dashed-folder glyph instead.

- `ui_sigil_create(parent, cx, cy, r)`: `r` is the outer radius, `UI_SIGIL_R_MIN` 22 to `UI_SIGIL_R_MAX` 110.
  The stroke is 3 px up to r 24, 4 px up to r 40, r / 10 above.
- `ui_sigil_set_name(s, name)`, `ui_sigil_set_color(s, colour)` (white until set), `ui_sigil_set_pos(s, cx, cy)`.
- Inside a screen's own draw callback: `ui_sigil_hash(name, h)` once, then
  `ui_sigil_paint(layer, h, cx, cy, r, w, colour)` (`w` 0 = the stroke for `r`).
- Sizes in use: r 22 on HOME (top) and the ambient face; r 52 as the subject of an orbit.
- `ui_sigil_set_pose(s, pose)`: the mark moved, resized, turned and dimmed by a Living-marks pose (§3.11); the
  shape stays the name's.

### 3.2 Glyphs: `ui_glyph_*`, `FONT_ICON_HERO` (128 px), `FONT_ICON_XXL` (96 px), `FONT_ICON_XL` (64 px)

Phosphor Fill, generated by `firmware/tools/fonts/gen.mjs`. The tick, cross, double tick and the two carets
come from Phosphor **Bold**, at every size, because the Fill family draws them inside a filled square (or as
a solid triangle). `ICON_CHECK`, `ICON_X`, `ICON_ALWAYS`, `ICON_NEXT`, `ICON_CARET_DOWN` are those.

- `FONT_ICON_S` / `M` / `L` / `XL` (20, 28, 44, 64 px) hold every icon.
- `FONT_ICON_XXL` (96 px, the glyph on a primary disc) and `FONT_ICON_HERO` (128 px, a state or a result)
  hold only the 16 glyphs that are ever one: working, hand, question, done, error, moon, hourglass, desktop,
  plugs, microphone, paper-plane, download, tick, cross, double tick, book (`HERO` in `gen.mjs`; add a name
  there to get another).
- `ui_glyph_create(parent, font, cx, cy)` and `ui_glyph_set(g, icons, colour)`: an icon centred on its **ink**
  (a label centres the font's line box, which is off by a few pixels at 128 px). Use it for the hero glyph.
  `icons` may be two or more icons side by side (a sign). `ui_glyph_set_pos(g, cx, cy)`.
- `ui_glyph_draw(layer, font, icons, cx, cy, colour, opa)`: the same inside a draw callback.
- `ui_glyph_font(icons, px)`: the largest icon font not above `px` that has every glyph of `icons`.

### 3.3 Time: `ui_dots_*`, `ui_tally_*`

- `ui_dots_create(parent, cx, cy, r)`, `ui_dots_set(d, minutes, colour)`: minutes as dots on a circle,
  clockwise from 12 o'clock, 6 degrees apart; a dot is 5 px, every tenth 9 px. Under a minute there are no
  dots. From 60 minutes on the circle is full: the minutes of the running hour are bright, the rest dim,
  and each whole hour is a pip 12 px inside the circle (up to 12). A new minute invalidates one dot.
- `ui_tally_create(parent, cx, cy)`, `ui_tally_set(t, minutes, colour)`: the same time where there is no room
  for a circle, on one row: a short bar for each hour (up to 9), a large dot for each ten minutes, a small
  dot for each minute. `ui_tally_width(minutes)` gives its width, to lay a glyph out beside it;
  `ui_tally_set_pos()`. HOME uses it for how long the watch has been offline.

### 3.4 Disc: `ui_disc_*`

A filled circle with a centred glyph: an action.

- `ui_disc_create(parent, cx, cy, r)`. Sizes: `UI_DISC_R_PRIMARY` 78 (glyph 96 px), `UI_DISC_R_SECONDARY` 52
  (64 px), `UI_DISC_R_ORBIT_SEL` 40 (44 px), `UI_DISC_R_SMALL` 30 (28 px), `UI_DISC_R_KEY` 20 (20 px, a
  2 px ring and no fill). A glyph the size's font lacks falls back to the next smaller font.
- `ui_disc_set_glyph(d, icon)`, `ui_disc_set_rgb(d, rgb)` (0 = neutral grey), `ui_disc_set_pos()`, `ui_disc_set_r()`.
- `ui_disc_set_state(d, st)`: `UI_DISC_NORMAL` (rgb set: filled in it, dark glyph; rgb 0: grey fill, light
  glyph), `UI_DISC_SELECTED` (accent fill, dark glyph), `UI_DISC_DISABLED` (dim).
- `ui_disc_set_hold(d, permille)`: the hold ring, an arc round the disc from 12 o'clock, 0 to 1000; 8 px
  outside the disc and 6 px wide, white. Each step invalidates only the arc it adds.
- `ui_disc_hit(d, x, y)`: the screen point is on the disc or within `UI_DISC_SLACK` (10 px) of it; false
  while it is hidden.
- A touch is a centre touch only inside r 150 of the screen centre (`NAV_CENTRE_R`); beyond it is the rim.
  A disc placed further out needs its tap let through, as `ui.c` does for HOME's book disc.

### 3.5 Orbit: `ui_orbit_*`

Up to 8 discs on a circle (`UI_ORBIT_R` 150) around the screen centre, one selected (r 40, accent), the
rest small and grey. The subject (a sigil, a clock, a glyph) is the screen's own, in the middle: the orbit
leaves r 0 to 105 free.

- `ui_orbit_create(parent)`, `ui_orbit_set_items(o, items, n)` with `ui_orbit_item_t {glyph, rgb, disabled}`
  (copied; up to `UI_ORBIT_ITEMS` 24), `ui_orbit_set_accent(o, rgb)`.
- Up to 8 items are spread evenly from 12 o'clock. More are shown 8 to a page, 45 degrees apart; the page
  of the selection is the one on screen, and pips under the subject say which.
- `ui_orbit_set_geometry(o, radius, phase_deg)`: with key hints on the screen and 8 items use phase 22, so
  the two upper discs straddle the hints. An arch whose phase is in the lower half runs left to right along
  the bottom.
- `ui_orbit_set_arch(o, span_deg, max_pitch_deg)`: an arch instead of the whole circle. The discs of a page
  sit on an arc of `span_deg` centred on the phase, the first at its left end, no further apart than
  `max_pitch_deg`. AGENT and READ centre it on 6 o'clock (phase 180) with 222 and 50: open at the top, the arc
  ends 24 degrees short of each key hint, and the subject and its text sit above it
  (`docs/design/glyphs/AGENT-READ.md`).
- The screen maps its input: the dial and button B to `ui_orbit_step(o, ±1)` (wraps, turns the page), a tap to
  `ui_orbit_hit(o, x, y)` (select that item, or run it when it is `ui_orbit_selected(o)`), button A to
  running the selection, a hold to `ui_orbit_set_hold(o, permille)`. `ui_orbit_count(o)`, `ui_orbit_select(o, i)`.

### 3.6 Keys: `ui_key_hint_*`

Rule 6: the glyph for A and for B, each in a thin ring at the rim.

- `ui_key_hint_create(parent, UI_KEY_A | UI_KEY_B)`, `ui_key_hint_set(hint, icon)`. A is at (101, 101) in amber
  (`RGB_KEY_A`), B at (365, 101) in blue (`RGB_KEY_B`), beside the keys on the upper rim; the ring is r 20 and
  reaches r 207 from the screen centre, clear of the countdown (r 213) and the index ring (r 218). KEYS uses
  the same places: its candidates begin below them.
- A hint is a disc: hide and show it like one; `ui_disc_set_hold()` fills a ring round it for a hold on the button.
- On HOME and WATCH a hint is also the key (issue 20261126-34e5): `ui_key_hint_hit()` (within `UI_KEY_HIT_R` 36
  of its centre, while shown) makes a tap `ZONE_KEY_A` / `ZONE_KEY_B`, and `ui_key_hint_flash()` lights its ring
  for 150 ms on a tap or a press of the key. `ui_key_hint_set_count(hint, n)`: a white badge r 11 on the
  hint's inner lower edge with the digit in black (rule 4), 0 hides it, 9 at most. A's counts the other
  sessions that need you, the pips on the ring; finished sessions are not counted.
- Above y 121 a row level with the hints is only as wide as the gap between them (x 125 to 341; ASK's title
  and AGENT's question are 216 px). The bottom slot, x 129 to 337 below y 345, is as narrow by the round
  glass alone: the toast, page dots and docks live there.
- `theme_key_badge()` and `theme_key_badge_set_word()` keep their signatures and draw this: the word is
  looked up in the vocabulary (§3.8); a word without a glyph draws a dot, never letters. So every screen
  that has not been rebuilt already shows the hints. A rebuilt screen calls `ui_key_hint_*` directly.

### 3.7 Burst and sign: `ui_burst`, `ui_sign_*`, `ui_toast`

- `ui_burst(words, icons, rgb)`: rule 7. A hero glyph (128 px) alone on a black centre for `UI_BURST_MS`
  (700 ms), then the screen below shows again. `words` ("Approved") goes to the report and the debug log,
  never to the glass; `icons` NULL takes `theme_glyph(words)`. The caller plays the haptic. It replaces the
  full-screen result cards.
- The burst covers the screen below. A screen that arms a hold when it appears (ASK) must not arm while
  `ui_burst_active()`.
- `ui_sign_create(parent, cx, cy)`, `ui_sign_set(s, icons, text, rgb)`: rule 8. One or two glyphs side by
  side in a thin outlined capsule, both in `rgb`, 56 px high. `text` is content shown after them in white
  (what was sent, a short number), or NULL.
- `ui_toast(words, rgb)` keeps its signature and its words (rule 9) and shows a sign from the vocabulary
  in the bottom slot for 1.8 s. `Sent "…"` shows the glyph and what was sent. Words that have no glyph yet
  draw the info glyph: add a row to the vocabulary instead.
- A screen that keeps content in the bottom slot (HOME does) is blanked there while a toast shows, so
  the sign never sits on half a row: add the screen's mode to `toast_blanks_slot()` in `ui.c`.

### 3.8 Vocabulary: `theme_glyph(word)`

One table from the UI's words to glyphs (`ui_theme.c`, `kGlyphs`). A meaning has one glyph on every
screen. A screen that needs a new meaning adds a row.

| Meaning | Glyph |
|---|---|
| Working / starting | arrows-clockwise |
| Needs approval | hand-palm |
| Has a question | chat-circle-dots |
| Done / error / idle, ended | check-circle / warning-circle / moon |
| Waiting | hourglass-low |
| On your computer, open on PC | desktop |
| Offline / connecting | plugs / the transport's glyph |
| Approve / deny / always allow | tick (bold) / cross (bold) / double tick |
| Send, sent / not sent | paper-plane / paper-plane and cross |
| Reply / dictate / keyboard | arrow-bend-up-left / microphone / keyboard |
| Read / activity / graph | book-open / pulse / git-fork |
| Snooze / unsnooze | moon (and the minutes as dots) / sun |
| Notify when done / don't | bell-ringing / bell-slash |
| Stop / run again / try again | stop-circle / arrow-clockwise / arrow-counter-clockwise |
| Hold to stop / hold to run again (a tap on a hold disc) | hand-tap and stop-circle / hand-tap and arrow-clockwise |
| Dismiss | cross |
| Open (menu) / next / back / more / select | dots / caret-right / arrow-left / caret-down / tick |
| Quiet / sound / mute | bell-slash / speaker / speaker-slash |
| Privacy full / preview / meta | eye / eye-closed / eye-slash |
| Sessions (count) | stack |
| Claude Code / Copilot CLI / cloud / terminal / pi / other | sparkle / airplane / cloud / terminal / pi (π) / robot |
| Not supported / not allowed on the PC | prohibit / desktop and prohibit |
| Timed out / no window / out of date | hourglass / app-window and cross / desktop and download |
| Updating | download |
| Nothing else needs you | check-circle |
| A toast or a key hint whose words have no glyph yet | info / dot |

### 3.9 Arc: `ui_arc_*`

Rule 4, a quantity as geometry.

- `ui_arc_create(parent, cx, cy, r, w)`: a round-capped arc on the circle of centre-line radius `r`, `w` px
  wide. `ui_arc_set_span(a, start_deg, span_deg)` (default: a whole circle from 12 o'clock).
- `ui_arc_set(a, permille, fill, track)`: a track over the span and a fill over `permille` of it; a black
  track draws none. Progress fills; a countdown drains (set it from 1000 down). A step invalidates only the
  piece between the old end and the new one.
- `ui_arc_set_steps(a, n)`: a level of `n` equal steps (a volume of 5). The arc is drawn as `n` pieces with
  gaps, and the fill lights whole pieces. 0 (the default) is one continuous arc.
- `ui_arc_set_geometry(a, cx, cy, r)`: moves the arc and changes its radius, change-only: an arc that follows
  something the screen lays out (STATUS's battery ring).

### 3.10 Pips: `ui_pips_*`

- `ui_pips_create(parent, cx, cy)`, `ui_pips_set(p, n, current)`: a row of `n` dots, the current one larger
  and white: which page of a reader or a transcript. One page draws nothing; more than 12 show 12 and the
  position in proportion. The orbit draws its pages with the same pips.

### 3.11 Living marks: `temper.c`, `persona.c`, `ui_living.c` (issue 20261126-98a6)

No character is added. The marks the watch already draws move by one grammar, so after a week a project is
known by how its mark breathes. Colour still means state only: a mark is never given a colour of its own.

| Meaning | Gesture | Sigil | Hero glyph | Duration |
|---|---|---|---|---|
| alive, working | breath | brightness `cos8` 72–100 % and the middle ring swayed 2–6° | still (too large to loop) | 3–6 s loop |
| idle | slow breath | brightness 80–100 % at twice the period, no sway | still | loop |
| wants you | lean, then **still** | 3 px toward the hero glyph (1, 2, 3 px over 200 ms), full bright | still | 200 ms, then nothing |
| arrived (done) | dip and settle | r × 0.90, × 1.06, back (`out3`) | 3 px down, 2 up, back | 300 ms |
| joy (done) | radial | — | eight sparks in the done colour leave it, r 70 → 104, fading | 450 ms |
| wrong (error) | shiver | sideways +2 −2 +2 −1 +1 0 px, 40 ms each, first the temper's way | the same at 3 px | 240 ms |
| asleep (snoozed) | droop | 60 % and the outer ring turned 6° the temper's way | still | static |
| ended, offline, handed back | still | as drawn | still | — |

Rules: one gesture at a time per mark (a new state cuts the old one); **nothing loops while anything needs
you**, so stillness says "waiting for you"; loops are slow and small, one-shots under 900 ms; the same name and
the same event always give the same gesture. A scale breath is invisible at r 22 (±0.66 px), so the breath is
brightness and sway; the radius moves only in the dip.

- **Temperament** (`temper.c`, pure): bytes 10..19 of the same SHA-256 the shape uses (bytes 0..9), from the
  same key (`temper_key()`: the folder name, or `"\x01sid N"` with none; `ui_sigil_set_session` uses it).
  t[5] the period (3.0–6.0 s in 250 ms steps), t[6] the sway (2–6°), t[7] bit 0 the sway period (3/2 or 2
  breaths), bit 1 the direction, t[8] the phase, t[9] the sparks' rotation. `temper_pose()`, `temper_hero()`
  and `temper_joy()` turn a state and the time in it into a pose; a pose never reaches more than 4 px past the
  rest, and the loop is sampled in sixteenths of a breath, so a breathing mark repaints at most 16 times a
  breath.
- **The gate** (`persona.c`, pure): see §3.12.
- **On screen** (`ui_living.c`): a screen binds its sigil and hero glyph to a slot (`ui_living_bind`) and says
  in its update what the slot shows (`ui_living_show`, `ui_living_state`); `app.c` hands the step's persona
  context to `ui_living_tick()` after `ui_tick()`. A mark out of sight rests; a newly selected session starts
  at rest, so scrolling onto a finished one does not dip it. `ui_sigil_set_pose()` is change-only; the sigil
  object is r + 6 px a side to hold a pose.
- Bound today: HOME's sigil (r 22) and hero glyph, and the ambient face's sigil (it breathes at 32–45 % on the
  dock; it helps burn-in rather than hurting it). AGENT and ASK hold still by rule; WATCH is not bound yet.
- **The ring** keeps its own motion, which already speaks the grammar: the comet is the working loop, done is
  one sweep then solid (settle), error one flicker then solid (shiver), idle thin and still, and the ambient
  ring is still. Its pulses on a request are the alert, not fun, so they stay; no fun gesture is drawn on the
  ring.
- Cost (simulator, `fun_temper.txt`): a breathing r 22 sigil adds about 95 k px in 6.5 s, about 15 k px a
  second (7 % of one full frame a second); the comet alone is about 500 k px in the same window. A finish
  repaints the hero glyph's box (about 12 k px) for 10 frames and the sparks' dots (about 1.6 k px a frame).
  Frame time and the AMOLED's look at desk distance are for the device (open questions in the issue).

### 3.12 The fun switchboard: `persona_allow()`, the `fun` word

Every fun feature asks `persona_allow(feature, ctx)` (or `ui_living_allow(feature)` from a screen) what it may
do now, and tests none of the conditions itself. It returns `PA_POSE` (a static pose), `PA_ONESHOT`,
`PA_LOOP`, `PA_HAPTIC`, `PA_SOUND`:

| Condition | Result |
|---|---|
| the feature's bit off in `fun`, or the display dark or only glowing | nothing |
| anything needs you (a request, a session waiting for input) | a finish keeps its buzz and sound, not its picture; the asking mark keeps its lean (`alerting`); all else a static pose at most |
| face down, silent, hush | no buzz, no sound |
| quiet (mode or setting) | no sound; a buzz only when the user started it or the event alerts |
| on battery | no loop (one-shots stay) |
| `FUN_REDUCED` | no loop, no one-shot: the end frame |
| neither started by the user nor riding on an alert | no buzz, no sound: fun never initiates |

The glue (`app.c`) fills the context once a step from the model, power, motion and `prefs_fun()`; a feature
copies it and sets `user_started` / `alerting` / `hush` for its own event. The stored word (`prefs_fun()`,
NVS key `fun`) has a bit per feature: `FUN_TEMPER` 0 (on), `FUN_CELEBRATE` 1 (on), `FUN_MOTIF` 2 (off),
`FUN_MOTE` 3 (off), `FUN_HELLO` 4 (on), `FUN_REPLAY` 5 (off), `FUN_EGGS` 6 (on), `FUN_TRAIL` 7 (on), `FUN_CUP`
8 (on), `FUN_TOYS` 9 (on), and `FUN_REDUCED` 15 (off). It is set in LABS > Personality until STATUS has rows
([EXPERIMENTS.md](EXPERIMENTS.md)); a script sets it with `pref fun 0x3d3`.

### 3.13 While you were away: `replay.c`, `ui_replay.c` (issue 20261126-9f99)

When you come back to the watch, the bezel replays the last hour and the centre shows now at once. A LABS
experiment: the Replay switch (`FUN_REPLAY`, bit 5) in LABS > Personality, off by default.

- **Away** is the display dark (off, or only the alert glow) for at least 15 minutes (`REPLAY_AWAY_MS`): a
  quarter of the hour replayed, and longer than a glance away. It is measured from when the display went
  dark, so on the dock (dark after 10 minutes idle) a replay needs about 25 minutes without input, on battery
  about 16. The **first look** is the first lit step after it, whatever lit it: a touch, a key or a raise.
- **The replay**: 20 steps of 3 minutes, 100 ms each (2 s; an hour in 2 s is 1800×). Each capsule shows its
  past state, settled and still, in its state's colour; a session not yet started is the empty ring's thin
  track. A thin white arc at r 216 (between the pips and the capsules) grows 18° a step from 12 o'clock, the
  hour's hand that marks the ring as a replay; at 2 s the capsules are now and the arc fades over 200 ms.
  The card, the hero glyph and everything in the centre show now from the first frame.
- **Skipped** unless `persona_allow(PF_REPLAY)` gives a one-shot (§3.12: anything that needs you, reduced
  motion, the switch off, a dark display), the deck is up and awake, and the hour holds at least one change.
  **Ended at once** by any touch, key or raise, by anything that comes to need you, or by leaving the deck. It
  only draws, so it never holds input back. No sound, no buzz.
- **History** (`replay.c`, pure, `test_replay.c`): 16 sessions × 20 columns of 3 minutes, about 470 bytes of
  static RAM (PSRAM on the watch). A column keeps the most telling state the capsule showed in its 3 minutes
  (error, then a request, then done, then work, then idle), so an orange flash that resolved itself is seen.
  The ring merges finished sessions from 9 on, so more than 16 capsules within an hour is rare; a session
  without a track replays as it is now, and the track of the session gone longest is reused first. It lives
  in RAM: after a deep sleep or a reboot the hour starts empty, and steps before the watch recorded show now.
- **On the ring**: `ring_override()` draws a segment in a past look without touching the model; it is
  change-only, so a step repaints only the capsules whose state differs plus the 18° of arc it adds.
  `ui_home_tick()` makes the one call (`ui_replay_tick`). Cost (simulator, `fun_replay.txt`): about 144 k px
  over 1.1 s of a replay in which the one capsule (a full ring) changes once; under 2 k px with the switch
  off. Whether 100 ms a step reads well, and the frame time, are for the device.

## 4. Reference screen: HOME

Built with the primitives, as the model for the rest (`ui_home_card.c`; renders:
`simulator/scripts/sigil_home.txt`).

- **Card (a session selected), top to bottom:**
  - The project's sigil at the top (r 22 at y 50, white). Merged finished sessions: the stack glyph, and
    their count under it.
  - Under it one small row (y 84): the agent's mark in grey, with a globe before it for a remote session
    and a moon after it when snoozed; the bell-slash when the watch is quiet; a failed job's exit status.
  - A sign takes that row when something is wrong, in colour, one 28 px glyph: offline is the plug in
    amber with how long as a tally (§3.3); connecting is the transport's glyph; a low battery is the
    battery-warning glyph in red. While offline the rest of the card is grey.
  - The state as a hero glyph (128 px, centre y 206), in the state colour.
  - Time in that state as dots round the glyph (r 88).
  - A job: its progress is an arc inside the dot circle (r 76).
  - The summary below, two rows (text: rule 1), the first 300 px wide, the second 240. A finished session
    shows the done line there (`ui_done.c`), or its last message. On a request: the request's title and
    two rows of its text (240 and 200 px: the last one is in the bottom slot).
  - A session that needs you: the hero glyph is the hand or the question, and a tap on it opens ASK (any
    tap in the centre does). No Answer / Review button; the report still says `Review` / `Answer`.
  - In the bottom slot, under the text: the agent graph strip (the summary then keeps one row), or the
    book disc (r 24) of a finished session with a last message, which opens READ; and the context gauge
    (`ui_gauges.c`), under whichever of them shows.
- **Empty deck:** the clock in digits, alone.
- **Ambient:** the clock, dim, with the selected session's sigil and a small state glyph under it.
- **Keys:** A = the desktop (it opens the session on the computer, as before), with a count badge of the
  other sessions that need you; B = caret (next), shown when there is more than one session. A tap on
  a shown hint presses its key. The session menu opens by a tap, as before.
- **The session ring** at the bezel is unchanged, except that a needs-you pip is a shape per kind: dot
  (question), bar along the ring (permission), triangle pointing in (error); UI-V3 §3.1. What waits behind the
  screen is a badge at 12 o'clock: the kind's glyph, or how many wait (UI-V2 §4.3).

## 5. The other screens

Each is rebuilt from nothing by one owner. "Functions" is what must survive; the layout is the
owner's, inside the rules.

| Screen (files) | Functions that must survive | Sigil form |
|---|---|---|
| **ASK** (`ui_ask.c`, `ui_countdown.c`) | Approve (1.2 s hold), deny (0.8 s hold), always allow with the rule shown (hold), open on PC, the agent's own options, multi-question answers with ticks, other / dictate, read all for long text, the answer window countdown, results (approved, always allowed, denied, sent, waiting for the computer, on your computer, time ran out), too long to approve here | The request text at the top (rule 1). Approve is the primary disc with its hold ring; deny a secondary disc with a cross; open on PC a small desktop disc. The agent's options are text rows (content). Results are bursts. The countdown is an arc. The rule's scope and width are glyphs beside the rule text |
| **AGENT** (`ui_agent.c`) | Open on PC, dismiss, snooze 30 / unsnooze, notify when done / don't, activity, reply (quick replies, dictate, keyboard), stop (hold), run again (hold), reply now, cancel queued, read, graph; the queued text | An orbit around the session's sigil, with the project name under it (rule 2). Quick replies are the user's own text rows. Holds use the disc's ring |
| **READ** (`ui_read.c`) | Read the last message page by page, reply, dictate, read again, open on PC, answer it, loading / not loaded | The message is text (a summary). Position is a row of page dots. Actions are discs after the last page |
| **WATCH** (`ui_watch.c`, `ui_wheel.c`) | The step timeline: step number, tool and argument, ok / fail / running, finished / stopped / waiting, no activity for a time; answer | Steps as a lane of glyph nodes like GRAPH (tool glyph, tick / cross / spinner); the selected step's argument is text. Time as dots |
| **STATUS** (`ui_status.c`) | Clock, host, battery, quiet (30 min, 1 h, off, until a time), mute, volume 0–5, about (sessions, waiting, errors, privacy, version), link state | The clock in digits. An orbit of toggles. Volume is an arc on its disc. Battery is an arc. About is counts in badges and the privacy eye; the version is the only text |
| **GAUGES** (`ui_gauges.c`, `gauges.c`) | Context fill, plan usage 5 h and week with pace and reset, model, permission mode, pull request and its state | Arcs. Mode and pull request are glyphs in the state colour. The model name is content from the agent and is dropped from HOME; it stays in the report |
| **DONE** (`ui_done.c`, `done.c`) | Files changed, lines added and removed, tests passed or failed, turn time | The files glyph with a count badge, a two-colour bar for added / removed, the flask with a tick or cross, time as dots |
| **VOICE** (`ui_voice.c`) | Record, level, stop, transcribing, the transcript with pages, use / send (hold), try again, queued, not sent | The microphone hero with a live level ring; the transcript is text (rule 1); discs for send, retry; page dots |
| **KEYS** (`ui_keys.c`) | Swipe typing, candidates, space, delete, send (hold), errors | Text entry (rule 1): keys and candidates stay letters. Space, delete and send are glyph keys. Errors are signs |
| **SCROLL** (`ui_scroll.c`) | Scroll the terminal by dial and drag, live / lines up, bottom, page, preview | A scroll track as an arc with a thumb; the live glyph at the bottom; the count of lines up as the thumb's position, not a number. The preview is text (terminal content) |
| **TRACKPAD, LABS** (`ui_trackpad.c`, `ui_labs.c`) | Move, click, right click, double click, wheel; how-to; refusal; the experiments list; the fun switches (`ui_fun.c`) | The pad is the screen: a faint cursor glyph; the how-to is three pictograms. Labs is an orbit; Personality an orbit of toggle discs, the off ones dim |
| **OTA** (`ui_ota.c`, `ota.c` toasts) | Updating with progress, restarting, waiting for the computer, hold B to cancel, the errors | The download hero inside a progress arc; restarting is the working glyph; cancel is B's key hint with a cross and a hold ring; an error is the error glyph and a number (rule 8) |
| **Toasts, glow** (`ui.c`, `app.c`) | Every toast | Signs through the vocabulary (§3.7) |
| **GRAPH** (`ui_graph.c`) | unchanged | Already the reference for lanes and nodes; only its key hints change |

## 6. How it is built

1. **Foundation:** the primitives of §3, the fonts, and HOME (§4) as the reference.
2. **Screens in parallel**, one owner per row group of §5, each in its own worktree on the foundation.
3. **Integration:** merge, the whole simulator suite, the firmware build, the Atlas web watch, and
   before / after sheets.

Checks for every screen:

- `ctest` green. A script's taps may move with the layout, and its `expect_label` words stay
  (rule 9). A script changes only where a tap target moved.
- No full-screen redraw on a tick: `expect_px_max` tests keep passing.
- The screen's `shot` renders are looked at, not just produced.
