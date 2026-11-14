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
   the physical button: A lower left in amber, B lower right in red. No letters.
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
amber and B's is red because the physical buttons are.

## 3. Primitives (`ui_kit.h`, built first, used by every screen)

A screen is assembled from these. A screen that needs something new adds it here, not locally.

### 3.1 Sigil: `ui_sigil_*`

A custom-drawn object: three concentric rings of arcs on an 8-sector grid and a core, all derived
from a hash of the name (`Scr.sigil()` in `docs/design/glyphs/mock.py` is the reference: per ring,
one hash byte picks the sectors, two more force at least two; runs of neighbouring sectors merge into
one arc; the core is a dot or a bar). Round caps. Any size from 22 px to 110 px radius, any colour.
An empty name (no folder) draws the dashed-folder glyph instead.

### 3.2 Hero glyph: `FONT_ICON_HERO` (128 px) and `FONT_ICON_XL` (64 px)

Phosphor Fill, generated by `firmware/tools/fonts/gen.mjs`. The hero size holds only the glyphs that
are ever a state or a result. The tick, cross and caret come from Phosphor **Bold**, because the Fill
family draws them inside a filled square.

### 3.3 Time dots: `ui_dots_*`

Custom-drawn: `n` minutes as dots on a circle of radius `r` about a centre, clockwise from 12
o'clock, 6 degrees apart; every tenth dot larger. Past 60 minutes the circle is full and each further
hour is a pip inside it. Under a minute there are no dots. Change-only: a new minute invalidates one
dot.

### 3.4 Disc: `ui_disc_*`

A filled circle with a centred glyph. Sizes: primary (r 78), secondary (r 52), small (r 30), key
(r 20, ring only). States: normal, selected (accent fill, dark glyph), disabled (dim), and a hold ring
(an arc around the disc, 0 to 1000 per mille). Hit test with a fingertip's slack.

### 3.5 Orbit: `ui_orbit_*`

Up to 8 discs on a circle around the screen centre, one selected (larger, accent). The bezel dial and
button B move the selection; a tap on a disc selects it, a tap on the selected disc or button A runs
it. The subject (a sigil, a clock, a glyph) sits in the middle. More than 8 items page.

### 3.6 Keys: `ui_keys_hint`

Rule 6: the glyph for A and for B, each in a thin ring at the rim. Replaces `theme_key_badge`.

### 3.7 Burst: `ui_burst`

Rule 7: a hero glyph in a colour for a moment, with the existing haptic. Replaces the word toasts and
the full-screen result cards. `ui_toast()` keeps its signature and its words (rule 9) and draws the
sign from the vocabulary; a toast that carries content (`Sent "…"`) shows the glyph and the content.

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
| Dismiss | cross |
| Open (menu) / next / back / more / select | dots / caret-right / arrow-left / caret-down / tick |
| Quiet / sound / mute | bell-slash / speaker / speaker-slash |
| Privacy full / preview / meta | eye / eye-closed / eye-slash |
| Sessions (count) | stack |
| Claude Code / Copilot CLI / cloud / terminal / other | sparkle / airplane / cloud / terminal / robot |
| Not supported / not allowed on the PC | prohibit / desktop and prohibit |
| Timed out / no window / out of date | hourglass / app-window and cross / desktop and download |
| Updating | download |

## 4. Reference screen: HOME

Built with the primitives, as the model for the rest.

- **Card (a session selected):**
  - The project's sigil at the top (r 26, white).
  - The state as a hero glyph in the centre, in the state colour.
  - Elapsed time as dots around the glyph.
  - The summary below, two lines (text: rule 1). On a request, the request's title and text.
  - A session that needs you: the hero glyph is the hand or the question, and a tap on it (or A)
    opens ASK. No Answer / Review button.
  - A finished session with a last message: a small book disc under the summary opens READ.
  - Offline: the card dims and a small plug sign with the age as dots sits under the sigil.
  - Low battery: the battery-warning sign in red.
  - A job: its progress is an arc inside the dot circle.
  - The context gauge and the done summary keep their place under the summary, as shapes (§5).
- **Empty deck:** the clock in digits, alone.
- **Ambient:** the clock, dim, with the selected session's sigil and a small state glyph under it.
- **Keys:** A = dots (menu), B = caret (next).
- **The session ring** at the bezel is unchanged.

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
| **TRACKPAD, LABS** (`ui_trackpad.c`, `ui_labs.c`) | Move, click, right click, double click, wheel; how-to; refusal; the experiments list | The pad is the screen: a faint cursor glyph; the how-to is three pictograms. Labs is an orbit |
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
