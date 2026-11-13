# UI v3: "Halo"

Status: **implemented in the simulator on branch `ui-v3`; not yet validated on the device** (issue 7732). Answers [UI-PROFILE.md](UI-PROFILE.md).
The interaction model, the modes and every safety rule of [UI-V2.md](UI-V2.md) stay; where this
document and UI-V2 disagree about how something *looks*, is *worded* or how you go *back*, this
document wins. Finding numbers (H1, R1, X2 …) refer to the profile.

## 1. Principles

1. **One purpose per element.** The ring is your agents. A button is something you press. Nothing
   on screen does two jobs (R1).
2. **Status by exception.** A healthy link, a charged battery and a live terminal are shown as
   nothing at all. Things appear when they need you (H3).
3. **One grammar.** Swipe right goes back, everywhere. The row you touch is the row you get. Hold
   means "this cannot be undone" and always looks the same (X2, X3, X4).
4. **Rank with weight, not size.** One typeface, two weights, five sizes. One accent colour per
   screen: the state of the thing you are looking at. Chrome is white and grey (H5, G2).
5. **No instructions on glass.** A hint appears only as the answer to a wrong input (a toast), never
   as a permanent caption (X1).
6. **Nothing is lost.** Every v2 action is still there, and every v2 gesture still works. v3 adds
   paths; it removes none.
7. **Glyphs over words (project tenet).** Use the least text possible. Show information as a glyph,
   colour, shape or position wherever one can carry it. Use a word only when no glyph can. Check
   every new screen against this first, and when a screen changes, look for text a glyph can
   replace. The agent graph ([GRAPH.md](GRAPH.md)) is the reference: lanes and glyphs, no text.

Safety invariants, unchanged and not negotiable in any later reskin: the approval card shows the
request text; a tap never approves or denies; an approve-class commit is a 1.2 s hold bound to the
request id, armed 300 ms after the card appears; deny is the 0.8 s hold or the strict long swipe;
the first touch during a glow only reveals; approve / deny / reject haptics differ by count; button
A never approves.

## 2. Tokens (`ui_theme.h`)

### 2.1 Type: Figtree (SIL OFL 1.1), generated with `lv_font_conv` at 4 bpp

| Token | Size / weight | Use |
|---|---|---|
| `FONT_CAPTION` | 20 / 500 | context line, legends, footers, step rows (the smallest text: 20 px, about 1.9 mm, legibility pass 20261112-491a) |
| `FONT_BODY` | 20 / 400 | summaries, prompt text, unselected rows |
| `FONT_BODY_STRONG` | 20 / 600 | toasts, chips, small buttons |
| `FONT_HEAD` | 24 / 600 | state word, selected row, card titles, buttons |
| `FONT_TITLE` | 34 / 600 | the subject of the screen (project, tool, result) |
| `FONT_NUM` | 56 / 600 | clock, timers, counters: glyphs `0-9 : - % .` and space only |

**Tabular digits.** Every Figtree face has its OpenType `tnum` feature frozen in before conversion
(`firmware/tools/fonts/tabular.py`, called by `gen.mjs`): the cmap entries for 0-9 point at the
tabular glyphs, so all ten digits share one advance and a ticking clock, timer, step time or
percentage never moves its neighbours. It applies to every size (a separate proportional set would
cost about 450 KB of flash); digits inside running text are the font's own tabular glyphs. The colon,
percent sign and space were already fixed. `simulator/tests/test_fonts.c` (ctest `tabular_digits`)
measures `1:11` against `0:00`, `11%` against `00%` and more in every size.

Glyph range for the text fonts: U+0020–007E, U+00A0–00FF, U+2013, U+2014, U+2018, U+2019, U+201C,
U+201D, U+2022, U+2026. Each text font falls back to the icon font of the nearest size, then to
nothing. `theme_font(px)` stays as a compatibility shim (≤17 → caption, ≤21 → body, ≤28 → head,
≤40 → title, else num) so a screen that has not been restyled still gets the new face. The
Montserrat sizes are switched off in `lv_conf.h` and `sdkconfig.defaults` once nothing names them.

### 2.2 Icons: Phosphor Fill (MIT), three sizes: 20, 28, 44

`ui_icons.h` defines one UTF-8 string per icon. (Phosphor Fill draws plain `check` and `x` inside
squares, so step marks use the circle variants.) No built-in `LV_SYMBOL_*` remains in a v3 screen.

| Macro | Phosphor name | Code | Used for |
|---|---|---|---|
| `ICON_WORKING` | arrows-clockwise | e094 | state: working |
| `ICON_QUESTION` | chat-circle-dots | e16c | state: has a question |
| `ICON_PERMISSION` | hand-palm | e57e | state: needs approval |
| `ICON_DONE` | check-circle | e184 | state: done; "Approved"; finished step (20 px, green) |
| `ICON_ERROR` | warning-circle | e4e2 | state: error; failures |
| `ICON_IDLE` | moon | e330 | state: idle; Snooze |
| `ICON_DENIED` | x-circle | e4f8 | "Denied"; Dismiss; failed step (20 px, vermillion) |
| `ICON_OFFLINE` | plugs | eb56 | offline chip (compared at 20 px with wifi-slash, cloud-slash, link-break: plugs is the clearest) |
| `ICON_BLUETOOTH` | bluetooth | e0da | link: BLE |
| `ICON_USB` | usb | e956 | link: USB |
| `ICON_COMPUTER` | desktop | e560 | link: transport unknown ("your computer") |
| `ICON_BATTERY_FULL/HIGH/MED/LOW/WARN/CHARGING` | battery-* | e0c0 e0c2 e0c6 e0c4 e0c8 e0ba | battery |
| `ICON_MIC` | microphone | e326 | Dictate, VOICE |
| `ICON_KEYBOARD` | keyboard | e2d8 | Keyboard |
| `ICON_BELL` | bell | e0ce | Notify when done |
| `ICON_QUIET` | bell-slash | e0d4 | Quiet |
| `ICON_SOUND` | speaker-high | e44a | sound on |
| `ICON_MUTE` | speaker-slash | e45a | sound off |
| `ICON_OPEN` | arrow-square-out | e5de | Open on PC |
| `ICON_ACTIVITY` | pulse | e000 | Activity (the live monitor) |
| `ICON_STOP` | stop-circle | e46e | Stop (interrupt) |
| `ICON_REPLY` | arrow-bend-up-left | e024 | Reply |
| `ICON_SENT` | paper-plane-tilt | e398 | "Sent" |
| `ICON_INFO` | info | e2ce | About |
| `ICON_LIVE` | arrow-line-down | e05c | SCROLL at the bottom |
| `ICON_BACKSPACE` | backspace | e0ae | KEYS |
| `ICON_HOLD` | circle | e18a | drawn as an outline ring, see §3.3 |

### 2.3 Colour

| Token | Hex | Use |
|---|---|---|
| `COL_BG` | `#000000` | everything that is not content: pixels off |
| `COL_TEXT` | `#FFFFFF` | primary text |
| `COL_TEXT2` | `#A0A0A8` | secondary text (`COL_MUTED` is an alias) |
| `COL_TEXT3` | `#6C6C73` | legends, footers, disabled |
| `COL_FILL1` | `#1C1C1E` | platter (text block, inactive toggle) |
| `COL_FILL2` | `#2C2C2E` | raised platter (selected row, toast, neutral button) |
| `COL_ON_ACCENT` | `#000000` | text and icons on a filled state colour |

State colours stay Okabe-Ito (`RGB_WORKING` sky, `RGB_ASK` yellow, `RGB_PERMISSION` orange,
`RGB_DONE` green, `RGB_ERROR` vermillion, `RGB_IDLE` grey). Rules: at most **one** state colour in
the centre of any screen; legends, hints, link and battery are never state-coloured; a filled accent
carries black text. `theme_dim(rgb, pct)` mixes toward black (ring segments at rest: 40 %).

### 2.4 Geometry

- Screen 466, centre (233, 233). The ring owns r 218–233. **Content stays inside r ≤ 205**:
  `theme_chord(y, margin)` already gives the width at a row; every label gets a width from it and
  `LV_LABEL_LONG_MODE_DOTS`, so nothing can touch the ring (L1).
- Row anchors shared by every screen (L3): context caption centred on **y 72**; legends on the
  **y 364** row; toast on the same row (it replaces the legends while it shows).
- Radii: capsules are fully round; platters 18 px.
- Motion: `UI_MOTION_MS 160`, ease-out. Entering a deeper mode, the incoming content rises 20 px
  into place; going back, nothing moves. Toasts rise 10 px. No opacity fades on large objects (cost
  on the device). All of it is off when `ui_animations_allowed()` is false.

### 2.5 Shared components

- **Legend** (`theme_key_badge`, X6): a 22 px outlined key cap (2 px border, LVGL borders are whole pixels; `COL_TEXT3`, letter in
  `FONT_CAPTION` `COL_TEXT2`) and a word in `FONT_CAPTION` `COL_TEXT2`, sentence case, never
  coloured. A hugs the left of the y 364 row, B the right, each anchored so its farthest corner is at
  r 212 (inside the ring, which starts at r 218; "6 px inside the chord" would put the cap on the ring),
  so they sit beside the physical buttons and leave the centre to content. A legend whose button does
  nothing in the current state is hidden.
- **Toast**: while it shows, a black pill (314 × 26, never beyond r 205 at that row) hides the legends
  under it and the ring stays intact. Capsule, `COL_FILL2`, `FONT_BODY_STRONG` white, a 10 px dot in the toast's colour on
  the left, height 40, max width 290, centred on y 364. One slot on every screen (G4, L3).
- **Chip**: icon 20 + text in `FONT_CAPTION` (not `FONT_BODY_STRONG`), no background; used for
  exceptions only.
- **Button**: capsule, height 48 (73 for a hold row, §3.2 and §3.3), `FONT_HEAD`. Prominent = filled accent, black text. Neutral =
  `COL_FILL2`, white text. A hold button carries the hold ring (a 20 px outlined circle) at its
  right end; while held, a lighter overlay grows from the left edge across the capsule, and the
  bezel shows the same progress as an arc (the finger usually covers the button).

## 3. Components

### 3.1 The ring: your agents, and only that (R1–R3)

- One round-capped capsule per session, clockwise from 12 o'clock, centre line r 225, 8° gaps.
- Width 6 at rest, **12 when selected**. No inner underline.
- Colour: the selected session and any session that needs you (question, permission, error) at
  100 %; everything else at 40 %. Working keeps its comet, Done its one sweep, needs-you its 1 Hz
  pulse.
- Not colour alone: an *unselected* session that needs you carries a **pip**, a filled 10 px dot in
  its colour at r 205 on the capsule's midpoint (the selected one is already wide and bright, so it has
  none). Dots and twin tracks are gone.
- The ring shows the index on **every** screen. While a hold is in progress the index drops to
  20 % and the hold arc (10 px, the item's colour, from 12 o'clock) is drawn over it.
- SCROLL keeps its position track (4 px `COL_FILL2` circle, accent arc).
- AGENT and ASK keep the ring still while they are open (the pixel budget of the device does not
  allow an animated ring under a list). AGENT and ASK no longer lay their rows out on the rim, and the rim tap that picked a row is gone
  with them (rows are tapped directly, §3.3). VOICE no longer draws loudness on the rim.

### 3.2 List (`ui_wheel`, style LIST): AGENT, Reply

Three rows, the selected one in the middle. In AGENT the selected capsule is 73 high (7 mm: the hold row
must be hit with a thumb) and the pitch 65; the layout's default is 56 high and pitch 62. The capsule is `COL_FILL2` and
330 wide; a 28 px icon at 22 px from its left edge; the label in `FONT_HEAD` white, left-aligned
64 px from the edge. Neighbours: no capsule, icon in `COL_TEXT3`, label in `FONT_BODY`
`COL_TEXT2`, same left edge. No chevrons (X5). A destructive row is vermillion (icon and label) and
shows the hold ring at the right end of the capsule.

### 3.3 Picker (`ui_wheel`, style BUTTON): ASK, VOICE send

Three rows. Selected: a button (§2.5), 280 wide and 73 high in ASK and VOICE send (pitch 54; the default layout is 48 high, pitch 48), label centred; filled with the item's
colour, or neutral for "Open on PC". Neighbours: `FONT_BODY` `COL_TEXT2`, centred. A hold item has
the hold ring; there is no `HOLD` tag.

**A hold acts on the row under the finger.** A press arms the hold only when it lands on the selected
row or on no row at all (a finger resting beside the button); a press on a neighbour row arms nothing, it
is the start of a tap on that row. In ASK and AGENT, taps are ignored until the screen has been open
`NAV_CARD_ARM_MS` (300 ms), so the tap that opened it, or a second one behind it, cannot land on a row.

**Tapping a row acts on that row** (X3): `wheel_hit(w, x, y)` returns the row under the point. A tap
on a neighbour selects it and, if it is safe, commits it; if it is a hold item it only selects it
and the toast says `Hold to approve` / `Hold to deny` / `Hold to stop`. A tap on the selected row or
anywhere else in the centre behaves as in v2. A tap still never commits a hold item.

## 4. Screens

### 4.1 HOME

```
            y 72    Claude Code · feat/login          caption, COL_TEXT2
            y 146   webapp                            FONT_TITLE white; FONT_HEAD ×2 lines if too wide
            y 196   ⟳ Working  1m 40s                 icon 28 + FONT_HEAD in state colour; time FONT_BODY COL_TEXT2
            y 232…  Running the test suite after      FONT_BODY COL_TEXT2, 2 lines
                    the login refactor
            y 320   (exception slot)                  chips, or the Answer button
            y 364   Ⓐ Open                    Next Ⓑ
```

- State words (W3): `Working`, `Has a question`, `Needs approval`, `Done`, `Error`, `Idle`,
  `Waiting` (input with no request). Snoozed appends `· snoozed` after the time. The time of a finished agent
  (`Done` / `Error`) is an age: `Done 1m ago`, `just now` under a minute (a job keeps its own times).
- A session that needs you shows its request from y 220 instead of the summary: a title line and two
  text lines (a request starts higher than a summary so that three lines clear the button).
- No action pill, no link row (H2, H3). The exception slot shows, in this order of precedence:
  `Offline · 4m` (amber chip) › low battery ≤ 15 % (vermillion chip `12%`) › an **Answer** button
  (prominent, the state colour, 160 × 44, centred on y 320, `Review` for a permission and `Answer` for a question)
  when the session needs you › a lone `ICON_QUIET` in `COL_TEXT3` while quiet runs › nothing.
- Legends: A `Open`, B `Next` (hidden with one session).
- Offline: the card drops to 50 % as in v2.
- **Empty**: the time in `FONT_NUM` at y 200 and `Nothing running` in `FONT_BODY` `COL_TEXT2` at
  y 262; before the clock is set, `Ready` in `FONT_TITLE`. No "swipe down for status" (M1).
- **Ambient** (30 s idle, nothing needs you): the ring at 20 %, the time in `FONT_NUM` at 45 %
  white, and one caption `webapp · Working` at the same level; before the clock is set, the v2
  dimmed card. The whole face drifts 1 px each minute round a 2 × 2 square (burn-in guard; new in v3,
  the v2 card had its own shift).

### 4.1b A job on the deck (2026-11-11, [JOBS.md](JOBS.md))

A terminal command that runs longer than ten seconds is a session like an agent's: same ring, same card.

```
            y 72    Terminal · main                   caption
            y 146   webapp                            subject
            y 196   ⟳ Running  about 2m left          state row; the time is the job's own (below)
            y 219   ━━━━━━━━━────                     160 × 4 capsule, state colour on COL_FILL2: only with an estimate
            y 232…  cargo test --workspace            the command line (the program under privacy meta)
```

- Words: `Running`, `Finished`, `Failed`. Time: the elapsed run time; `about 2m left` under an estimate
  (10 s steps below a minute, whole minutes above, rounded up); `in 3m 12s` when finished;
  `exit 101 · 42s` when failed. Past the estimate the bar stays full and the elapsed time returns.
- The bar changes a pixel at a time and only when its width changes (redraw discipline, UI-V2 §8a).
- A job that ends always buzzes (unless snoozed or quiet).
- Menu (`nav_job_items`): `Stop` (hold, danger) while it runs; `Run again` (hold, sky) and `Dismiss` after.
  Opening the menu does not acknowledge a job: its ack is `Dismiss`, which removes the card. A one-item
  menu hides the B legend.

### 4.1c Gauges on the deck (2026-11-12, [GAUGES.md](GAUGES.md))

- A Claude session with a known context fill gets a 160 × 4 capsule in the job bar's row (y 219): grey, amber
  from 70 %, vermillion from 90 %. Hidden under a request, for a merged slot and for a job.
- The empty deck and the ambient face show `5h 42%` (`FONT_BODY`, y 304) while the plan's 5-hour figure is
  known and fresh; amber from 80 %.

### 4.1d The done summary (2026-11-12, [DONE-SUMMARY.md](DONE-SUMMARY.md))

- A finished (done / error) Claude session with a summary draws one line at y 246 in place of the step text:
  `3 files · +120 –14 · ✓ passed · 4m 12s`, `FONT_BODY` `COL_TEXT2`, at most 372 px. Parts: files, lines, tests,
  time; unknown ones are not drawn. Failed tests alone are vermillion (`✗ tests failed`). Too wide: `tests
  passed` shortens to `passed`, then the lines, the time and the files drop, in that order; the tests stay.
- Not for jobs, a request, a merged slot or "On your computer". The Activity footer (y 322, `FONT_CAPTION`)
  shows the same line for a finished session.

### 4.1e The last message on the deck (2026-11-12, [LAST-MESSAGE.md](LAST-MESSAGE.md))

- A finished Claude session whose daemon sent its last message (proto 0.18, not under privacy meta) shows the
  message's start in place of the step text and the done summary line: `FONT_BODY` **white**, two lines from y 232,
  dotted. The exception slot holds a neutral `Read` button (`COL_FILL2`, white `FONT_HEAD`, 160 × 44 on y 320) when
  nothing outranks it (offline, low battery, a request).
- A tap on the button opens READ (§4.10); a tap anywhere else on the card opens AGENT as before, where `Read` leads
  the list (`ICON_QUESTION`, the speech bubble). No pop-up and no extra buzz: the finished buzz is as before.

### 4.2 AGENT

Caption y 72: the project (host first when remote). The list (§3.2) centred on y 232 (250 under a question). Items and
order as v2 §4.2, relabelled: **Activity** (was Watch), **Notify when done** / **Don't notify**,
**Snooze 30 min** / **Unsnooze**, **Stop** (was Interrupt, hold), **Reply**, **Dismiss** (was
Dismiss error), **Open on PC** (was Focus terminal), **Dictate**, **Keyboard** (was Type…).
In the Reply list with a question, the question sits at y 64–112 (`FONT_BODY_STRONG` white, two
lines), the context caption is hidden (the question owns the top of the screen) and the list is
centred on y 246. Legends: A `Select`, B `Next`.

### 4.3 ASK

```
            y 58    Claude Code · webapp              caption
            y 80    Run shell command                 FONT_HEAD white, one line
            y 112…  ┌──────────────────────────┐      platter COL_FILL1, r 18, 320 wide, pad 10/14
                    │ npm test -- --watch=false │      FONT_BODY white, up to 3 lines
                    └──────────────────────────┘      (a question's text: no platter, COL_TEXT2)
            y 226   Open on PC                        picker neighbour (touch zone y 209-243)
            y 280   ████████ Approve ○ ████████        the button, 73 high (y 244-316); orange; hold ring
            y 334   Deny                              picker neighbour (touch zone y 317-351)
            y 364   Ⓐ Open on PC              Next Ⓑ
```

- Colours: grant = orange, deny = vermillion, a question's option = yellow, Open on PC = neutral.
  Only the selected button is coloured.
- No rule, no hint line, no `HOLD` tag (L4, X1, X4).
- After the answer (M3), positions are top edges: `ICON_DONE` / `ICON_DENIED` / `ICON_SENT` at 44 px in
  the answer's colour at y 154, the word (`Approved`, `Denied`, `Sent`) in `FONT_TITLE` white at y 215,
  and `Waiting for your computer` in `FONT_CAPTION` `COL_TEXT2` at y 266 (y 256 with the option's text
  above it). The request platter is hidden to make room. The answered state stays at least 900 ms after the
  answer was sent, even if the daemon clears the request sooner; a new request or any input takes over at once. No second pill. A sent option
  shows its text as the caption's first line (`"Vue"`).
- Legends: A `Open on PC`, B `Next` (hidden with one option).

#### 4.3a Read before you approve (issue f6fc)

A permission can only be approved when all of its text has been on screen.

- A text of up to three lines is shown whole, as above, and nothing changes.
- A longer text shows two lines and `Read all` (`FONT_CAPTION`, the request's colour) inside the platter.
  A tap on the platter opens the **reader**: caption and title stay, and the text fills a window 320 wide
  and seven lines tall from y 118, with `2 of 3` under it when there is more than one page.
- In the reader: tap, swipe up or B is the next page, and on the last page returns to the picker (B reads
  `More`, then `Done`). Swipe down is the previous page. Swipe right or B held closes the reader and stays
  on ASK. A is still `Open on PC`. Nothing in the reader answers the request: a swipe down is never the
  deny swipe there and holds do nothing. A tap within 300 ms of a page appearing is ignored.
- Until the last page has been shown for this request and this text, an item that grants permission
  cannot be held. A tap on it opens the reader; a hold says `Read it first`. Deny and `Open on PC` are
  never locked.
- A text the daemon cut is not the whole request. A 0.11 daemon says so with `len`; for an older one the
  watch goes by the text (it ends in `…`, or it fills the model's buffer). The platter then shows `Read all`
  even when the preview fits, and the reader fetches the **full text** by request id (the `detail` request,
  [ANSWER-WINDOW.md](ANSWER-WINDOW.md) §4, up to 4096 bytes) and pages through it as it arrives: `3 of 12+`
  while more is coming, `12 of 12 · loading` on the last page so far (B stays there), then `3 of 20`. The
  reader shows the text exactly: line breaks kept, control and undrawable characters spelled out (`^[`,
  `\u{a0}`), never a description in place of the command.
- Approve unlocks only when the last page of the whole text has been shown for this request. A cut text the
  watch can never have in full (an older daemon, privacy `meta`, past 4096 bytes, a refused or failed
  transfer) can be read (its preview) and denied on the watch; Approve answers `Too long to approve here`.
  (`ASK_BLOCK_CUT_TEXT` in `ui_ask.c`.) A chunk for another request, out of order, or disagreeing with the
  announced length never reaches the reader.

#### 4.3b The answer window (2026-11-12, [ANSWER-WINDOW.md](ANSWER-WINDOW.md))

- A thin ring just inside the index ring (centre r 214.5, 3 px, round caps, `COL_TEXT2`, white in the last
  10 s) shortens toward 12 o'clock as the time to answer on the watch runs out, on ASK and on the request's
  card on the deck. No text. Hidden once an answer is out, and for a daemon that sends no window.
- When the window ends ASK shows, for 3 s, `ICON_COMPUTER` (44 px, `COL_TEXT2`) at y 154, `On your computer`
  (`FONT_TITLE`) at y 215 and `Time ran out here` / `Answer it there` (`FONT_CAPTION`) at y 256; then the deck,
  where the card's state word reads `On your computer` with `ICON_COMPUTER` and the summary line
  `Answer it there`, until the session moves on. Any input on that ASK state goes to the deck.
- A then reads Open on PC as before, and also hands the request back to the terminal at once (the card then says
  `On your computer` · `Answer it there`). Once the window is over a hold cannot start, and a running one is
  cancelled.

#### 4.3c Always allow (2026-11-12, [STANDING-RULES.md](STANDING-RULES.md))

A permission whose request carries a standing rule (proto 0.13) has a third option after Deny:
`Approve` · `Deny` · `Always allow` · `Open on PC`. The default view is unchanged (`Open on PC` / Approve / `Deny`).

```
            y 58    Claude Code · webapp              caption (unchanged)
            y 80    Saved for this project            FONT_HEAD white: where (or "Until this session ends")
            y 112…  ┌──────────────────────────┐      platter COL_FILL1, r 18, 320 wide, pad 10/14
                    │     Bash(npm test:*)     │      FONT_BODY white: the rule as saved, ≤ 2 lines, never dotted
                    │ Wide: * matches anything │      FONT_CAPTION: orange when wide, else "Exactly as written" COL_TEXT2
                    └──────────────────────────┘
            y 226   Deny                              picker neighbour
            y 280   █████ Always allow ○ █████        73 high, orange, hold ring; NAV_GRANT_HOLD_MS 1600 ms
            y 334   Open on PC                        picker neighbour
```

- The panel replaces the request platter only while the row is selected; any other row puts the request back.
- A tap on the row says `Hold to always allow`. The row is locked, like Approve, until a long request text has
  been read (§4.3a), and for good when the rule does not fit two lines or the scope title does not fit its row:
  the panel then reads `This rule is too long to show on the watch` · `Answer it on your computer` and a hold
  says `Rule too long for the watch`.
- The hold is bound to the rule on screen (its hash goes with the approve) and cancelled if the rule changes; a
  re-sent request opens again at Approve.
- Answered: `Always allowed` (`FONT_TITLE`), then the rule's first line and the scope in the caption, held at
  least 2.5 s (900 ms for the other answers).

#### 4.3d Questions (2026-11-12, [LAST-MESSAGE.md](LAST-MESSAGE.md) §3)

An `AskUserQuestion` request (proto 0.18, `qs`) is shown one question at a time:

```
            y 58    Claude Code · webapp · 1 of 2     caption; "1 of 2" only with several questions
            y 80    Framework                         FONT_HEAD white: the question's header
            y 112…  Which UI framework should the     FONT_BODY COL_TEXT2, up to 3 lines (question style, no
            dashboard use?  Read all                  platter); "Read all" when an option has a description
            y 226   Other                             picker neighbour
            y 280   ██████ React ○ ██████             73 high, yellow, hold ring: a single-choice option
            y 334   Vue                               picker neighbour
```

- Rows: the options in the agent's order, then `Other` (dictation, when the daemon transcribes), then `Send`
  (`Next` before the last question; multi-select only), then `Open on PC`. All yellow except Open on PC.
- Single choice: the option is a hold row (`NAV_HOLD_MS` 1.2 s, the rising ramp); a tap only selects it and says
  `Hold to choose`. Multi-select: options are tap rows that tick (`ICON_DONE` before the label) and untick; `Send`
  is the hold row, `Tick at least one` while nothing is ticked.
- `Other` opens VOICE (`Answer for webapp`), whose hold button reads `Use` (`Send` on the last question) and hands
  the recording back; the transcript paging rule of §4.7 applies.
- Each answer moves to the next question (re-armed: taps ignored 300 ms); swipe right goes to the previous one,
  its answer kept, and from the first leaves ASK. The last answer sends all of them; the answered state reads
  `Sent` with `"Vue"` (one question) or `2 answers`.
- The reader (`Read all`) shows the question and each option's description, `React: Component library`.

### 4.4 WATCH (shown to the user as "Activity")

Caption y 72. Up to three earlier steps from y 108, pitch 26, `FONT_CAPTION` `COL_TEXT2`, each led
by a 20 px `ICON_DONE` (green) or `ICON_DENIED` (vermillion), as one left-aligned block centred on
the screen. The current tool in `FONT_TITLE` white with its top at y 210, one line ellipsized to the
chord (`Running the test suite` fits; longer ends in dots); its argument in `FONT_BODY` `COL_TEXT2`,
two lines, top at y 252. Footer on y 322 in `FONT_CAPTION` `COL_TEXT3`: `Step 3 · 1:37`. No hint
line. When the session is not working: the state icon at 44 px on y 176, the state word in
`FONT_TITLE` on y 236, a caption on y 278 (`Finished 3m ago`), and for a session that needs you the
**Answer** button on y 318. Legends: A `Open`, B `Next`.

### 4.5 SCROLL

Caption y 72. Scrolled: the number of lines above the bottom in `FONT_NUM` white on y 196 (no minus
sign) and `lines up` in `FONT_BODY` `COL_TEXT2` on y 246 (W5). Live: `ICON_LIVE` 44 px in the accent
on y 176 and `Live` in `FONT_TITLE` on y 236. The preview line in `FONT_BODY` `COL_TEXT2` on y 296,
one line. No chevrons, no hint. Legends: A `Bottom`, B `Page`, both hidden while live (neither does anything there).

### 4.6 STATUS: the control centre (M2)

```
            y 116   12:30                             FONT_NUM white (clock unset: host name, FONT_TITLE)
            y 168   🖥 dev-mac   ▮ 80%                 chip row, COL_TEXT2 (amber `Offline · 4m`)
            y 246   ( 🔔 ) ( 🔊 ) ( ⑤ ) ( i )      four round toggles, Ø 64, centres x 107 / 191 / 275 / 359
            y 316   Quiet: off                        FONT_BODY COL_TEXT2: what the selected toggle is
            y 364   Ⓐ Quiet 30 min            Next Ⓑ
```

- Toggles: **Quiet**, **Mute**, **Volume**, **About** (`ICON_INFO`). Each icon shows the current state, not the
  action (issue 20261113-c36c): Quiet is a bell (`ICON_BELL`) while alerts are on and a crossed-out bell
  (`ICON_QUIET`) while quiet runs; Mute is a speaker (`ICON_SOUND`) while sounds are on and a crossed-out
  speaker (`ICON_MUTE`) while they are off or silent. Off: `COL_FILL1` disc, white icon. On (silenced):
  filled sky, black icon. Selected: a 2 px white ring 5 px outside
  the disc. Tapping a toggle selects and commits it; scrub and B move the selection; A commits.
- Quiet cycles off → 30 min → 1 h → off; the line reads `Quiet: off` / `Quiet until 14:30`.
  Mute toggles the alert mode; the line reads `Sounds: on` / `Sounds: off` / `Silent`. About shows
  two lines: `2 sessions · 1 waiting` and `Privacy: preview · 0.4.2`; committing it does nothing. About
  stands in for the v2 Privacy row (v2 had it as its own list entry; v3 has four toggles).
- Volume (issue 20261111-4843) steps the stored speaker level (`prefs.h`, 1 → 5, then back to 1; 0, set
  only by a script, steps to 1). The disc shows the level as a `FONT_TITLE` digit inside a 4 px, 300°
  dial (gap at 6 o'clock) filled to the level in sky over a `COL_FILL2` track; both go `COL_TEXT3` while
  sounds are off. The line reads `Volume 3 of 5`, with ` (muted)` / ` (silent)` while Mute is on. A
  commit plays a tick and the `input` cue at the new level, and unmutes (stepping the level means the
  user wants to hear it); Mute stays the one-tap way to silence. Volume is not a silencer, so its disc
  is never filled.
- With the clock unset the title is the host name, so the chip row drops the host text and shows only
  the link icon and the battery.
- Legends: A names the commit (`Quiet 30 min`, `Quiet 1 h`, `Quiet off`, `Mute`, `Unmute`, `Volume 4`; hidden
  on About), B `Next`.

### 4.6b STATUS with usage (2026-11-12, [GAUGES.md](GAUGES.md))

```
            r 192 / 182   two 240° arcs through 12 o'clock, 4 px: outer 5 hours, inner the week
            y 84    12:30                             FONT_TITLE (FONT_NUM without usage)
            y 120   🖥 dev-mac   ▮ 80%                 chip row
            y 152   5h 42%  resets in 2h 10m          FONT_BODY_STRONG figure + FONT_BODY detail
            y 180   Week 18%  resets in 3d 5h
            y 246…  toggles, description, legends as §4.6
```

Arc fill white, amber from 80 %, vermillion from 95 %; an unknown or stale window is a dash over its track. In
AGENT (§4.2) a readout line sits at y 108: `Opus 4.7 · Plan · PR 128 approved`. Without any usage reading STATUS is
exactly §4.6.

### 4.7 VOICE

Caption y 72: `Reply to webapp`. **Listening**: a yellow disc Ø 96 on y 186 with a black
`ICON_MIC` (44 px), behind it a halo disc in yellow at 25 % whose radius follows the loudness on a
square-root curve (speech sits at 10–30 of 100 and must still move it), 48 → 80 px so its lower edge
stays clear of the timer; the elapsed time in `FONT_TITLE` white on y 292. Legend A `Stop`.
**Transcribing**: the disc in `COL_FILL2`, `Transcribing…` in `FONT_HEAD` on y 292. Only A (`Stop`,
listening) has a legend: a short B press does nothing here (B held is back), so no `Back` legend.
**Transcript**: the text in `FONT_BODY` white, up to five lines, as wide as the chord allows (at most
330 px), centred on y 170; a prominent yellow hold button `Send` (§2.5) on y 296 (73 high). A tap answers with the
toast `Hold to send`. **A transcript longer than the five-line box is paged** (issue c6cf): five lines at
a time, a tap or A (legend `More`) turns the page, and the button is a neutral `More 1/2` until the last
page has been on screen; only then does it become `Send`, so a hold never sends words that were not
shown. A hold before that sends nothing and toasts `Tap to read the rest` (amber). After the last page a
tap goes round to the first again.
**Failed**: `ICON_ERROR` 44 px on y 176, the reason in `FONT_HEAD` on y 236, a neutral button
`Try again` on y 300.

### 4.8 KEYS

Owned by issue 1e57: v3 changes only what the shared tokens change (typeface, legends, toast).
Finding L2 is logged on that issue.

### 4.9 UPDATING

Full-screen overlay on the top layer while a firmware update is open (`ui_ota.c`, driven by `ota.c`).
Progress ring at r 226, width 10: grey track, blue arc from 12 o'clock (green when done). Title
`Updating` (`FONT_TITLE`), the version in `FONT_CAPTION` when given, the percentage in `FONT_NUM`,
hint `Hold B to cancel` on the legend line. Link loss adds an amber `Waiting for the computer…`.
On done the title becomes `Restarting…` and the watch restarts after 1 s. On bad, failed, cancelled or
timed-out transfers the overlay closes and a toast shows the reason. All gestures are swallowed while
open except the B hold. The ring is drawn with `lv_draw_arc`; `LV_USE_ARC` and `LV_USE_BAR` stay off.

### 4.10 READ: the last message (2026-11-12, [LAST-MESSAGE.md](LAST-MESSAGE.md))

```
            y 58    Claude Code · webapp              caption
            y 80    Last message                      FONT_HEAD white
            y 112…  Built and flashed the new         FONT_BODY white, left-aligned, 320 wide, seven lines a page
                    reader. The watch now shows …
            y 300   1 of 3                            FONT_CAPTION COL_TEXT2: "2 of 5+" / "5 of 5 · loading" while it arrives
            y 364   Ⓐ Open                    More Ⓑ  B reads "Reply" on the last page
```

- Opened by the card's `Read` or AGENT's `Read`; it marks a done session seen. The text is the daemon's cleaned
  message, fetched by session (`cmd detail` without a rid) as the reader opens; the preview shows until it arrives.
- A tap, swipe up or B turns the page; a drag down goes back a page; a tap within 300 ms of a page appearing is
  ignored. Past the last page: the end picker (title `Answer it`, the message's last words above it in
  `FONT_BODY` `COL_TEXT2`, the ASK picker below): `Dictate` (yellow, opens VOICE on the session; it types, or
  queues when the agent works again), `Reply` (yellow, the AGENT Reply list), `Read again`, `Open on PC`. These
  rows only open screens: a tap is enough, and nothing is sent from READ itself.
- Back (swipe right, B held, a finger held still) closes the end picker first, then READ. A leaves to the PC.
  A new prompt (the message is cleared) or a new message closes READ.

## 5. Interaction changes (`nav.c`)

| Change | Rows | Why |
|---|---|---|
| **Swipe right in the centre = back** in AGENT, ASK, WATCH, SCROLL, STATUS and VOICE | `{MODE_x, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}` | X2. In ASK it leaves the request pending, exactly like B held. It is never deny. In SCROLL it is ignored when the same touch already scrolled (a sideways flick is not a back). |
| Every v2 way back stays as an alias | none removed | principle 6 |
| A tap on a list row acts on that row | handled in the screen via `wheel_hit`; the binding stays `NAV_COMMIT` | X3 |
| `NAV_PICK_RIM` leaves AGENT and ASK | two rows removed | the rim no longer shows rows (§3.1) |

Nothing else in the gesture table changes. HOME keeps left/right for sessions; KEYS keeps touch for
strokes and B held for back.

### 5.1 Moving through a list, and back (2026-11-11)

Touch is the main input, so a list (AGENT, ASK, the STATUS toggles) moves three ways without a button:

- **Drag up or down in the centre**: the list follows the finger, one item per 40 px (`NAV_LIST_DRAG_PX`);
  dragging up brings the next item in. A moving finger cancels any hold in progress.
- **Slide along the bezel**: a dial, one item per 40° (`NAV_LIST_SECTORS` 9), clockwise = next, anywhere on
  the rim. (Before, the rim was divided by the number of sessions, so with one session it did nothing.)
- **Tap a row** (§3.3).

Because a vertical drag now moves the list, **no swipe denies and swipe down no longer goes back** in AGENT
and ASK. Deny is only its 0.8 s hold; `NAV_DENY` is gone from `nav.c` and `extract.py` fails if it returns.

**Back** is swipe right, B held, or **a finger held still for 600 ms anywhere off the rows** (AGENT, ASK,
STATUS; in the ASK reader it closes the reader). As a consequence a hold item arms only when the press is
on its own row: a press beside the button no longer arms Approve.

## 6. Words

| v2 | v3 |
|---|---|
| `FOCUS`, `FOCUS + ACK`, `Focus terminal` | `Open` (legend), `Open on PC` (row) |
| `Question` / `Approve?` | `Has a question` / `Needs approval` |
| `Watch` | `Activity` |
| `Interrupt` | `Stop` |
| `Dismiss error` | `Dismiss` |
| `Reply…`, `Dictate…`, `Type…` | `Reply`, `Dictate`, `Keyboard` |
| `Hold to confirm` | `Hold to approve`, `Hold to deny`, `Hold to stop`, `Hold to send`, `Hold to always allow` |
| `Yes, don't ask again` | `Always allow`, with the rule, `Saved for this project` / `Until this session ends`, `Wide: * matches anything` / `Exactly as written`; answered: `Always allowed` |
| `Approved · waiting for PC` | `Approved` + `Waiting for your computer` |
| `-142 lines` / `above the bottom` | `142` / `lines up` |
| `3 steps · 1:37` | `Step 3 · 1:37` |
| legends in capitals | sentence case |

## 7. Build order and ownership

| Stage | Scope | Files |
|---|---|---|
| A | Fonts, icons, tokens, legend, toast, motion helper, ring, wheel (both styles, hit test, hold fill), swipe-right back | `firmware/tools/gen_fonts.*`, `core/src/fonts/`, `ui_icons.h`, `ui_theme.*`, `ui_ring*`, `ui_wheel.*`, `ui.c`, `nav.c`, `lv_conf.h`, CMake |
| B1 | HOME, empty, ambient | `ui_home.c`, `ui_home_card.c`, `ui_home_internal.h` |
| B2 | AGENT and Reply | `ui_agent.c` |
| B3 | ASK | `ui_ask.c` |
| B4 | WATCH and SCROLL | `ui_watch.c`, `ui_scroll.c` |
| B5 | STATUS and VOICE | `ui_status.c`, `ui_voice.c` |
| C | Review against this spec from rendered sheets; fixes; Atlas facts; docs | all |

Each stage keeps `ctest` green. Simulator scripts change only where a label or a coordinate
changed; no assertion about what is sent to the daemon (`"op":…`) is weakened.
