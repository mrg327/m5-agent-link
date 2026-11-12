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
  `Waiting` (input with no request). Snoozed appends `· snoozed` after the time.
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
- A text the daemon cut (it ends in `…`, or it fills the model's buffer) can never be shown whole. It can
  be read and denied on the watch; Approve answers `Too long to approve here`. (`ASK_BLOCK_CUT_TEXT` in
  `ui_ask.c`.) The fix for that limit is the `detail` request, which is not built yet.

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
            y 246   ( 🔕 )  ( 🔇 )  ( i )             three round toggles, Ø 76, centres x 143 / 233 / 323
            y 316   Quiet: off                        FONT_BODY COL_TEXT2: what the selected toggle is
            y 364   Ⓐ Quiet 30 min            Next Ⓑ
```

- Toggles: **Quiet** (`ICON_QUIET`), **Mute** (`ICON_MUTE`), **About** (`ICON_INFO`). Off:
  `COL_FILL1` disc, white icon. On: filled sky, black icon. Selected: a 2 px white ring 5 px outside
  the disc. Tapping a toggle selects and commits it; scrub and B move the selection; A commits.
- Quiet cycles off → 30 min → 1 h → off; the line reads `Quiet: off` / `Quiet until 14:30`.
  Mute toggles the alert mode; the line reads `Sounds: on` / `Sounds: off` / `Silent`. About shows
  two lines: `2 sessions · 1 waiting` and `Privacy: preview · 0.4.2`; committing it does nothing. About
  stands in for the v2 Privacy row (v2 had it as its own list entry; v3 has three toggles).
- With the clock unset the title is the host name, so the chip row drops the host text and shows only
  the link icon and the battery.
- Legends: A names the commit (`Quiet 30 min`, `Quiet 1 h`, `Quiet off`, `Mute`, `Unmute`; hidden
  on About), B `Next`.
- Issue 4843 (volume) adds a fourth control here; the row has room at Ø 64.

### 4.7 VOICE

Caption y 72: `Reply to webapp`. **Listening**: a yellow disc Ø 96 on y 186 with a black
`ICON_MIC` (44 px), behind it a halo disc in yellow at 25 % whose radius follows the loudness on a
square-root curve (speech sits at 10–30 of 100 and must still move it), 48 → 80 px so its lower edge
stays clear of the timer; the elapsed time in `FONT_TITLE` white on y 292. Legend A `Stop`.
**Transcribing**: the disc in `COL_FILL2`, `Transcribing…` in `FONT_HEAD` on y 292. Only A (`Stop`,
listening) has a legend: a short B press does nothing here (B held is back), so no `Back` legend.
**Transcript**: the text in `FONT_BODY` white, up to five lines, as wide as the chord allows (at most
330 px), centred on y 170; a prominent yellow hold button `Send` (§2.5) on y 296 (73 high). A tap answers with the
toast `Hold to send`. **A transcript that does not fit the five-line box is never sent from here**: it
would be cut with dots on glass while the hold sent all of it. A tap or a press on `Send` then toasts
`Too long to send from here` (amber) and nothing is sent; the hold never arms.
**Failed**: `ICON_ERROR` 44 px on y 176, the reason in `FONT_HEAD` on y 236, a neutral button
`Try again` on y 300.

### 4.8 KEYS

Owned by issue 1e57: v3 changes only what the shared tokens change (typeface, legends, toast).
Finding L2 is logged on that issue.

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
| `Hold to confirm` | `Hold to approve`, `Hold to deny`, `Hold to stop`, `Hold to send` |
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
