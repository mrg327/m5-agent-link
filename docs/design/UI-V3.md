# UI v3: "Halo"

Status: **in progress on branch `ui-v3`** (issue 7732). Answers [UI-PROFILE.md](UI-PROFILE.md).
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
| `FONT_CAPTION` | 16 / 500 | context line, legends, footers |
| `FONT_BODY` | 20 / 400 | summaries, prompt text, unselected rows |
| `FONT_BODY_STRONG` | 20 / 600 | toasts, chips, small buttons |
| `FONT_HEAD` | 24 / 600 | state word, selected row, card titles, buttons |
| `FONT_TITLE` | 34 / 600 | the subject of the screen (project, tool, result) |
| `FONT_NUM` | 56 / 600 | clock, timers, counters: glyphs `0-9 : - % .` and space only |

Glyph range for the text fonts: U+0020–007E, U+00A0–00FF, U+2013, U+2014, U+2018, U+2019, U+201C,
U+201D, U+2022, U+2026. Each text font falls back to the icon font of the nearest size, then to
nothing. `theme_font(px)` stays as a compatibility shim (≤17 → caption, ≤21 → body, ≤28 → head,
≤40 → title, else num) so a screen that has not been restyled still gets the new face. The
Montserrat sizes are switched off in `lv_conf.h` and `sdkconfig.defaults` once nothing names them.

### 2.2 Icons: Phosphor Fill (MIT), three sizes: 20, 28, 44

`ui_icons.h` defines one UTF-8 string per icon. No built-in `LV_SYMBOL_*` remains in a v3 screen.

| Macro | Phosphor name | Code | Used for |
|---|---|---|---|
| `ICON_WORKING` | arrows-clockwise | e094 | state: working |
| `ICON_QUESTION` | chat-circle-dots | e16c | state: has a question |
| `ICON_PERMISSION` | hand-palm | e57e | state: needs approval |
| `ICON_DONE` | check-circle | e184 | state: done; "Approved" |
| `ICON_ERROR` | warning-circle | e4e2 | state: error; failures |
| `ICON_IDLE` | moon | e330 | state: idle; Snooze |
| `ICON_CHECK` | check | e182 | finished step |
| `ICON_X` | x | e4f6 | failed step |
| `ICON_DENIED` | x-circle | e4f8 | "Denied"; Dismiss |
| `ICON_OFFLINE` | wifi-slash | e4f2 | offline chip |
| `ICON_BLUETOOTH` | bluetooth | e0da | link: BLE |
| `ICON_USB` | usb | e956 | link: USB |
| `ICON_LINK` | plugs-connected | eb5a | link: other |
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

- **Legend** (`theme_key_badge`, X6): a 22 px outlined key cap (1.5 px `COL_TEXT3`, letter in
  `FONT_CAPTION` `COL_TEXT2`) and a word in `FONT_CAPTION` `COL_TEXT2`, sentence case, never
  coloured. A hugs the left of the y 364 row, B the right, each starting 6 px inside the chord, so
  they sit beside the physical buttons and leave the centre to content. A legend whose button does
  nothing in the current state is hidden.
- **Toast**: capsule, `COL_FILL2`, `FONT_BODY_STRONG` white, a 10 px dot in the toast's colour on
  the left, height 40, max width 290, centred on y 364. One slot on every screen (G4, L3).
- **Chip**: icon 20 + text in `FONT_CAPTION`, no background; used for exceptions only.
- **Button**: capsule, height 48, `FONT_HEAD`. Prominent = filled accent, black text. Neutral =
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
- Not colour alone: a session that needs you carries a **pip**, a filled 5 px dot in its colour at
  r 207 on the capsule's midpoint. Dots and twin tracks are gone.
- The ring shows the index on **every** screen. While a hold is in progress the index drops to
  20 % and the hold arc (10 px, the item's colour, from 12 o'clock) is drawn over it.
- SCROLL keeps its position track (4 px `COL_FILL2` circle, accent arc).
- AGENT and ASK no longer lay their rows out on the rim, and the rim tap that picked a row is gone
  with them (rows are tapped directly, §3.3). VOICE no longer draws loudness on the rim.

### 3.2 List (`ui_wheel`, style LIST): AGENT, Reply

Three rows, pitch 62, the selected one in the middle. Selected: a `COL_FILL2` capsule 56 high and
330 wide; a 28 px icon at 22 px from its left edge; the label in `FONT_HEAD` white, left-aligned
64 px from the edge. Neighbours: no capsule, icon in `COL_TEXT3`, label in `FONT_BODY`
`COL_TEXT2`, same left edge. No chevrons (X5). A destructive row is vermillion (icon and label) and
shows the hold ring at the right end of the capsule.

### 3.3 Picker (`ui_wheel`, style BUTTON): ASK, VOICE send

Three rows, pitch 48. Selected: a button (§2.5), 280 wide, label centred; filled with the item's
colour, or neutral for "Open on PC". Neighbours: `FONT_BODY` `COL_TEXT2`, centred. A hold item has
the hold ring; there is no `HOLD` tag.

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
            y 232…  Running the test suite after      FONT_BODY COL_TEXT2, 2 lines (3 when it needs you)
                    the login refactor
            y 318   (exception slot)                  chips, or the Answer button
            y 364   Ⓐ Open                    Next Ⓑ
```

- State words (W3): `Working`, `Has a question`, `Needs approval`, `Done`, `Error`, `Idle`,
  `Waiting` (input with no request). Snoozed appends `· snoozed` after the time.
- No action pill, no link row (H2, H3). The exception slot shows, in this order of precedence:
  `Offline · 4m` (amber chip) › low battery ≤ 15 % (vermillion chip `12%`) › an **Answer** button
  (prominent, the state colour, 160 × 44, `Review` for a permission and `Answer` for a question)
  when the session needs you › a lone `ICON_QUIET` in `COL_TEXT3` while quiet runs › nothing.
- Legends: A `Open`, B `Next` (hidden with one session).
- Offline: the card drops to 50 % as in v2.
- **Empty**: the time in `FONT_NUM` at y 200 and `No agents running` in `FONT_BODY` `COL_TEXT2` at
  y 262; before the clock is set, `Ready` in `FONT_TITLE`. No "swipe down for status" (M1).
- **Ambient** (30 s idle, nothing needs you): the ring at 20 %, the time in `FONT_NUM` at 45 %
  white, and one caption `webapp · Working` at the same level; before the clock is set, the v2
  dimmed card. Pixel shift as in v2.

### 4.2 AGENT

Caption y 72: the project (host first when remote). The list (§3.2) centred on y 226. Items and
order as v2 §4.2, relabelled: **Activity** (was Watch), **Notify when done** / **Don't notify**,
**Snooze 30 min** / **Unsnooze**, **Stop** (was Interrupt, hold), **Reply**, **Dismiss** (was
Dismiss error), **Open on PC** (was Focus terminal), **Dictate**, **Keyboard** (was Type…).
In the Reply list with a question, the question sits at y 64–112 (`FONT_BODY_STRONG` white, two
lines) and the list is centred on y 246. Legends: A `Select`, B `Next`.

### 4.3 ASK

```
            y 62    Claude Code · webapp              caption
            y 92    Run shell command                 FONT_HEAD white, one line
            y 114…  ┌──────────────────────────┐      platter COL_FILL1, r 18, 320 wide, pad 10/14
                    │ npm test -- --watch=false │      FONT_BODY white, up to 3 lines
                    └──────────────────────────┘      (a question's text: no platter, COL_TEXT2)
            y 234   Open on PC                        picker neighbour
            y 282   ████████ Approve ○ ████████        the button; orange; hold ring
            y 330   Deny                              picker neighbour
            y 364   Ⓐ Open on PC              Next Ⓑ
```

- Colours: grant = orange, deny = vermillion, a question's option = yellow, Open on PC = neutral.
  Only the selected button is coloured.
- No rule, no hint line, no `HOLD` tag (L4, X1, X4).
- After the answer (M3): `ICON_DONE` / `ICON_DENIED` / `ICON_SENT` at 44 px in the answer's colour
  on y 176, the word (`Approved`, `Denied`, `Sent`) in `FONT_TITLE` white on y 236, and
  `Waiting for your computer` in `FONT_CAPTION` `COL_TEXT2` on y 278. No second pill. A sent option
  shows its text as the caption's first line (`"Vue"`).
- Legends: A `Open on PC`, B `Next` (hidden with one option).

### 4.4 WATCH (shown to the user as "Activity")

Caption y 72. Up to three earlier steps from y 108, pitch 26, `FONT_CAPTION` `COL_TEXT2`, each led
by a 20 px `ICON_CHECK` (green) or `ICON_X` (vermillion), as one left-aligned block centred on the
screen. The current tool in `FONT_TITLE` white on y 222; its argument in `FONT_BODY` `COL_TEXT2`,
two lines, from y 252. Footer on y 322 in `FONT_CAPTION` `COL_TEXT3`: `Step 3 · 1:37`. No hint
line. When the session is not working: the state icon at 44 px on y 176, the state word in
`FONT_TITLE` on y 236, a caption on y 278 (`Finished 3m ago`), and for a session that needs you the
**Answer** button on y 318. Legends: A `Open`, B `Next`.

### 4.5 SCROLL

Caption y 72. Scrolled: the number of lines above the bottom in `FONT_NUM` white on y 196 (no minus
sign) and `lines up` in `FONT_BODY` `COL_TEXT2` on y 246 (W5). Live: `ICON_LIVE` 44 px in the accent
on y 176 and `Live` in `FONT_TITLE` on y 236. The preview line in `FONT_BODY` `COL_TEXT2` on y 296,
one line. No chevrons, no hint. Legends: A `Bottom`, B `Page`.

### 4.6 STATUS: the control centre (M2)

```
            y 116   12:30                             FONT_NUM white (clock unset: host name, FONT_TITLE)
            y 168   ᛒ dev-mac   ▮ 80%                 chip row, COL_TEXT2 (amber `Offline · 4m`)
            y 246   ( 🔕 )  ( 🔇 )  ( i )             three round toggles, Ø 76, centres x 143 / 233 / 323
            y 316   Quiet: off                        FONT_BODY COL_TEXT2: what the selected toggle is
            y 364   Ⓐ Quiet 30 min            Next Ⓑ
```

- Toggles: **Quiet** (`ICON_QUIET`), **Mute** (`ICON_MUTE`), **About** (`ICON_INFO`). Off:
  `COL_FILL1` disc, white icon. On: filled sky, black icon. Selected: a 2 px white ring 5 px outside
  the disc. Tapping a toggle selects and commits it; scrub and B move the selection; A commits.
- Quiet cycles off → 30 min → 1 h → off; the line reads `Quiet: off` / `Quiet until 14:30`.
  Mute toggles the alert mode; the line reads `Sounds: on` / `Sounds: off` / `Silent`. About shows
  two lines: `2 sessions · 1 waiting` and `Privacy: preview · 0.4.2`; committing it does nothing.
- Legends: A names the commit (`Quiet 30 min`, `Quiet 1 h`, `Quiet off`, `Mute`, `Unmute`; hidden
  on About), B `Next`.
- Issue 4843 (volume) adds a fourth control here; the row has room at Ø 64.

### 4.7 VOICE

Caption y 72: `Reply to webapp`. **Listening**: a yellow disc Ø 96 on y 186 with a black
`ICON_MIC` (44 px), behind it a halo disc in yellow at 25 % whose radius follows the loudness
(48 → 88 px); the elapsed time in `FONT_TITLE` white on y 292. Legend A `Stop`.
**Transcribing**: the disc in `COL_FILL2`, `Transcribing…` in `FONT_HEAD` on y 292.
**Transcript**: the text in `FONT_HEAD` white, up to four lines, centred on y 170; a prominent
yellow hold button `Send` (§2.5) on y 300. A tap answers with the toast `Hold to send`.
**Failed**: `ICON_ERROR` 44 px on y 176, the reason in `FONT_HEAD` on y 236, a neutral button
`Try again` on y 300.

### 4.8 KEYS

Owned by issue 1e57: v3 changes only what the shared tokens change (typeface, legends, toast).
Finding L2 is logged on that issue.

## 5. Interaction changes (`nav.c`)

| Change | Rows | Why |
|---|---|---|
| **Swipe right in the centre = back** in AGENT, ASK, WATCH, SCROLL, STATUS and VOICE | `{MODE_x, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}` | X2. In ASK it leaves the request pending, exactly like B held. It is never deny. |
| Every v2 way back stays as an alias | none removed | principle 6 |
| A tap on a list row acts on that row | handled in the screen via `wheel_hit`; the binding stays `NAV_COMMIT` | X3 |
| `NAV_PICK_RIM` leaves AGENT and ASK | two rows removed | the rim no longer shows rows (§3.1) |

Nothing else in the gesture table changes. HOME keeps left/right for sessions; KEYS keeps touch for
strokes and B held for back.

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
