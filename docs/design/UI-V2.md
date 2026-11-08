# UI v2: "Deck": switch context, pick from a list, answer from the watch

Status: **decided, Phase 1 in progress** (branch `ui-v2`). Supersedes the Ring UI ([UI-RING.md](UI-RING.md)) and DESIGN.md §6, §6a (device side) and §8 (palette). Inputs: the Ring UX review (simulator pass, 2026-11-07), the user's v2 brief and answers (§9), and `docs/research/{stopwatch-interaction-capabilities,wearable-glance-gestures,minimal-peripherals}.md`.

Where this document and DESIGN.md or UI-RING.md disagree, this document wins. §10 lists each decision.

## 1. Principles

The watch answers one question per glance and one gesture per intent. **Context switching is the navigation**: the home screen is one session at a time, and the only thing you do on it is change which session (rim scrub, button B, side swipe) or open that session's own screen. Everything else is a **short list you move through with detents and commit from the centre**: the agent's actions, the agent's own answer options, the watch's settings. Lists are the one UI element, so the same thumb motion (scrub to move, tap to pick, hold to commit something irreversible) works everywhere, and the centre's vertical swipe means exactly one thing per mode. Safety is structural, not cosmetic: approvals show what is being approved, a tap never approves or denies, destructive commits are holds bound to a request id with a distinct haptic, and the first touch after a wake only wakes. Pixels are mostly off (AMOLED), motion is reserved for "needs you", and colour is never the only cue.

## 2. Navigation model

Five modes, one screen each. HOME is the only place you can stay; every other mode returns to HOME on commit, on **B long**, on the **A+B chord** (Home), or on timeout.

```
                 lift / button / touch-wake              attention arrives
   ┌─────┐ ──────────────────────────────► ┌──────────┐ ───────────────────► ┌──────────┐
   │ OFF │ ◄──────── 10 s idle ─────────── │   HOME   │ ◄── commit/back ──── │   ASK    │
   │glow │   (glow 2 s on attention,       │  (deck)  │                      │ card+list│
   └─────┘    first touch = wake only)     └──────────┘                      └──────────┘
                                          │  ▲    │  ▲
                        tap centre /      │  │    │  │ swipe down
                        swipe up          ▼  │    ▼  │ ("Watch…" item too)
                                     ┌─────────┐  ┌──────────┐
                                     │  AGENT  │  │  STATUS  │  link · battery · quiet mode
                                     │ (wheel) │  └──────────┘
                                     └─────────┘
                                          │  "Scroll terminal" item
                                          ▼
                                     ┌──────────┐
                                     │  SCROLL  │  vertical swipes drive the terminal
                                     └──────────┘
```

- **HOME (deck).** One session card in the centre, a thin pinned index ring at the rim. Scrub, B or a side swipe changes the session. Button A does the state's default action (Focus). Tap centre or swipe up opens AGENT.
- **AGENT.** The per-agent screen (user decision §9.1): a curved list ("wheel") of that session's actions, default item preselected, with gesture shortcuts layered on it. Scrub moves, tap commits a safe item, hold commits a destructive one.
- **ASK.** Opened automatically when an attention (question, permission) arrives while the display is on, or on the first touch during a glow. It is AGENT with the agent's prompt text above the wheel and the agent's options as items. Done/error attentions do not open ASK; they update the card and buzz.
- **SCROLL.** Vertical swipes drive the focused terminal's scrollback. Entered only from the AGENT wheel.
- **STATUS.** Link, host, battery, quiet mode. Swipe down from HOME, the "Watch…" item, and HOME's empty state.

Timeouts: AGENT 8 s, ASK none (it waits for the answer or a `clear`), SCROLL 8 s after the last swipe, STATUS 8 s. The display turns off 10 s after the last input on HOME unless an unanswered attention is showing.

## 3. Gesture map

One table, every input × mode. "—" means ignored (and logged). Thresholds: long-press 600 ms, hold ≥ 1200 ms (daemon floor 1000 ms), swipe ≥ 60 px within 400 ms, **deny swipe ≥ 120 px starting above the centre line**, rim annulus r 160–260 px, centre r < 150 px, scrub detent per item.

| Input | OFF / glow | HOME (deck) | AGENT (wheel) | ASK (card + wheel) | SCROLL | STATUS |
|---|---|---|---|---|---|---|
| First touch after off/glow | wake only (glow: reveal ASK) | | | | | |
| Tap centre | wake | open AGENT | commit selected item (safe items; a destructive item shows "hold") | commit selected item if safe (Open on PC, a question option); else shows "hold" | exit, keep terminal position | back to HOME |
| Long-press centre (600 ms) | wake | — (reserved) | — | — | — | — |
| Hold centre ≥ 1.2 s (arc + ramp) | — | — | commit a destructive item | **Approve**-class item (bound to `rid`), or Deny (0.8 s); arc colour = item colour | — | — |
| Rim scrub CW / CCW | wake | next / previous session (detent) | next / previous item (detent) | next / previous option (detent) | — | next / previous row |
| Tap rim | wake | select the session under the finger | select the item at that angle (items are laid out on the ring) | same | — | — |
| Vertical drag in centre | wake | — | — | — | scroll lines: 1 detent per 24 px = 3 lines | — |
| Swipe up (centre) | wake | open AGENT | — | — | flick: page up (older) | back to HOME |
| Swipe down (centre) | wake | open STATUS | back to HOME | **Deny** (permission only, strict) | flick: page down (newer) | — |
| Swipe left / right | wake | next / previous session | — | — | — | — |
| Button A short | wake | default action (Focus; Done also acks) | commit selected item (safe only) | commit selected item (safe only) | to bottom (live) and exit | commit row (quiet 30 min, …) |
| Button A double | wake | jump to next attention (Input › Error › Done-unseen) | — | — | — | — |
| Button A long | wake | — | — | — | — | — |
| Button B short | wake | next session | next item | next option | page down (newer) | next row |
| Button B long | wake | previous session | back to HOME | back to HOME (request stays pending) | back to HOME | back to HOME |
| A+B chord | wake | — (already home) | **Home** | **Home** (request stays pending) | **Home** | **Home** |
| Lift (IMU, later) | wake; during a glow opens ASK | — | — | — | — | — |
| Shake (IMU) | — | — | — | — | — | — |

Why this is conflict-free:
- The centre and the rim are disjoint hit regions (r < 150 vs r ≥ 160); a press between them is a rim press. A scrub that starts in the annulus never becomes a swipe; a swipe or drag that starts in the centre never becomes a scrub.
- **The centre's vertical motion has one meaning per mode:** HOME opens things (up = AGENT, down = STATUS), AGENT/ASK go back or deny (down only), SCROLL scrolls. Session switching is horizontal (left/right) or on the rim, so it can never be mistaken for a deny or a page flick. Deny exists only in ASK, and only as the strict swipe or the hold item.
- The wheel is moved by the rim and B, never by a vertical drag, so the wheel and the deny swipe cannot overlap.
- Holds only start in modes where the selected item is destructive. On HOME nothing is destructive, so no centre press can approve anything.
- IMU is never a commit input (research: no trusted false-positive data). Shake is reserved.

## 4. Screens

Display 466 × 466, centre (233, 233). Fonts: Montserrat 16/20/24/32 (existing). Palette: Okabe-Ito for state, Primer-dark surfaces (§10). Every state has colour, a word, and a glyph.

| State | Colour | Glyph (centre) | Index-ring motion | Word |
|---|---|---|---|---|
| Working | sky `#56B4E9` | `LV_SYMBOL_REFRESH` | slow comet | Working |
| Needs input | yellow `#F0E442` | `?` | pulse 1 Hz | Question |
| Permission | orange `#E69F00` | `LV_SYMBOL_BELL` until a custom `!`-in-circle glyph exists | pulse 1 Hz, double line | Approve? |
| Done | green `#009E73` | `LV_SYMBOL_OK` | one sweep, then dim after ack | Done |
| Error | vermillion `#D55E00` | `LV_SYMBOL_WARNING` | dashed, drawn **inset** from the band | Error |
| Idle | grey `#999999` | `LV_SYMBOL_PAUSE` | none | Idle |

### 4.1 HOME: the deck

Glanceable: *which* session, *what state*, *what it just did*. One gesture deeper: everything else.

```
                     ╭──────────────────────────────────────╮
                  ╭──╯  ▬▬  ▬▬▬▬  ▬▬  ▬▬▬▬▬  ▬▬  ▬▬▬  ▬▬  ╰──╮    index ring, 6 px, pinned order,
                ╭─╯                                          ╰─╮  selected = 12 px + white tick
               ╭╯            Claude Code · feat/login          ╰╮  16 px muted: agent · branch
              ╭╯                                                ╰╮
              │                  webapp                          │ 32 px white: cwd basename (2 lines max)
              │                                                  │
              │               !  Approve?                        │ 32 px state colour + glyph
              │                                                  │
              │      Run shell command:                          │ 20 px muted, 2 lines: summary / title
              │      npm test -- --watch=false                   │ (truncates with "…")
              │                                                  │
              │                  2m 28s                          │ 20 px muted: time in this state
              ╰╮                                                ╭╯
               ╰╮   [A] FOCUS                      NEXT [B]    ╭╯  Pebble-style key badges
                ╰─╮                                          ╭─╯
                  ╰──╮        ◦ BLE · dev-mac  ·  82 %     ╭──╯  16 px: link · host · battery
                     ╰──────────────────────────────────────╯
```

- The index ring replaces the Ring UI's 10–18 px band. It is a 6 px arc per session, pinned by creation order from 12 o'clock, 4° gaps; the selected one is 12 px with a 2 px white radial tick at its midpoint (the tick, not an inner outline, marks selection). At ≥ 9 sessions, acknowledged Done sessions merge into one dim arc, as today.
- The state row is the largest coloured element; elapsed time is demoted to 20 px and always means "time in this state" (`tst`).
- The summary row shows `session.summary` for Working/Done/Error and `attention.title` + `text` for Question/Permission, so the approval text is already on the glanceable screen (carried from `2702775`).
- Key badges `[A] WORD` / `WORD [B]` sit at 7:30 and 4:30 beside the physical buttons. They are legends, not touch targets; a tap there is a rim tap.
- Offline: the centre drops to 50 % opacity, the badges grey out, elapsed freezes, and the link row reads `Offline · stale 4m`.
- Ambient (30 s idle, nothing needs you): ring 20 %, centre 30 %, pixel shift 1 px per minute. Carried over.
- Empty: the centre shows STATUS (§4.5).

Before/after reference: the Ring screenshots `20_mixed_error.png`, `30_perm_card.png` and `50_long_repo_work.png` (UX review scratch directory) show the hierarchy this fixes: elapsed as large as the repo, no prompt text, one-line truncation.

### 4.2 AGENT: the per-agent screen

User decision §9.1: tapping the centre opens a screen where gestures act on that one agent. **The screen is the wheel, with gestures layered on it, not a blank gesture pad.** A gesture pad makes every command invisible: the user must remember that "swipe left = snooze" and gets no confirmation of what is about to fire, which is exactly the discoverability failure the research warns about (watchOS gives each scene one visible primary action; the Actions Ring and Surface Dial show the menu while you gesture). The wheel keeps every available action on screen with its state, while the gestures are shortcuts that bypass the list: hold = the destructive item, swipe down = deny/back, A = default, B/scrub = move. Each row carries its shortcut tag so the two layers teach each other.

```
              ╭──────────────────────────────────────╮
           ╭──╯                                      ╰──╮
         ╭─╯            Claude Code · webapp            ╰─╮   16 px header: which agent and session
        ╭╯                                                ╰╮
        │                  Snooze 30 min                   │  20 px muted (previous)
        │                                                  │
        │         ▶     Focus terminal        [A] ◀        │  28 px white, state-coloured chevrons, shortcut tag
        │                                                  │
        │                Scroll terminal                   │  20 px muted (next)
        │                Notify when done                  │  16 px muted
        │                Watch…                            │
        ╰╮                                                ╭╯
         ╰─╮  [A] SELECT                     NEXT [B]  ╭─╯
           ╰──╮                                      ╭──╯
              ╰──────────────────────────────────────╯
```

- 3–5 items visible, the selected one in the centre row; the list wraps. Scrub or B moves it with a detent per item. Items are also laid out as arcs on the index ring while AGENT is open (one arc per item, selected = thicker), so a rim tap picks directly.
- Destructive items carry a trailing `HOLD` tag and commit only with a 1.2 s hold; a tap on them shows a 1 s hint "hold to confirm" and does nothing else.
- Items per state (default first):

| State | Items (default first) | Destructive (hold) |
|---|---|---|
| Working | Focus terminal · Scroll terminal · Snooze 30 min · Notify when done · Watch… | — |
| Question | Focus terminal · *the agent's options (ASK)* · Scroll terminal · Watch… | — |
| Permission | Open on PC (review) · Approve · *grant options* · Deny · Scroll terminal · Watch… | Approve, grants, Deny |
| Done | Focus + acknowledge · Acknowledge · Scroll terminal · Watch… | — |
| Error | Focus terminal · Dismiss · Scroll terminal · Watch… | — |
| Idle | Focus terminal · Snooze 30 min · Watch… | — |

- Items the daemon has not advertised (`welcome.feats`, `attention.acts`, or a previous `result.err = unsupported`) are hidden, not greyed.
- Deferred: "Cancel agent" (interrupt the running turn). It would be a hold item here; no decision yet.

### 4.3 ASK: the agent's question

```
              ╭──────────────────────────────────────╮
           ╭──╯        Claude Code · webapp            ╰──╮
         ╭─╯                                            ╰─╮
        ╭╯   Run shell command                            ╰╮  24 px white: attention.title
        │    npm test -- --watch=false                     │  20 px muted, up to 3 lines: attention.text
        │                                                  │  (tap the text = request `detail`, full text scrolls)
        │  ────────────────────────────────────────────── │
        │                Yes, don't ask again   HOLD       │  options from attention.opts, or
        │     ▶          Approve                HOLD  ◀    │  Approve / Deny for a bare permission
        │                Deny                   HOLD       │
        │                Open on PC             [A]        │
        ╰╮                                                ╭╯
         ╰─╮  [A] OPEN ON PC               NEXT [B]    ╭─╯   A never approves: it is the review path
           ╰──╮    long swipe down to deny           ╭──╯
              ╰──────────────────────────────────────╯
```

- The prompt text is the top half of the screen; the options are the wheel in the bottom half. Nothing is hidden behind a gesture.
- **Approve-class options (anything that grants permission, including "don't ask again" variants) commit only by hold.** The hold starts after 250 ms, and only once the card has been on screen 300 ms (`CARD_ARM_MS`); it fills a 10 px arc in the item's colour from 12 o'clock over the index ring, with the escalating haptic ramp; early release cancels; the hold is bound to `rid` and cancels if the attention changes or is cleared.
- Deny commits by hold (0.8 s, falling haptic) **or** by the strict swipe down (≥ 120 px, starting above the centre line). Both answer with `cmd_op_deny`.
- Question options (`attention.opts`, ≤ 4 × 24 chars) commit by tap or A; they are not destructive. The reply is `cmd_op_reply` with `arg = option index`.
- After an answer: the wheel collapses to one line `Approved · waiting for PC` (or `Denied`, `Sent "Yes"`) in the state colour; the index-ring arc stops pulsing and dims; the card stays until `clear`, then HOME returns. A `result.ok = false` shows the error and restores the options.
- Nag: up to 3 re-buzzes every 15 s while a permission is unanswered and the display is off; any input stops it.

### 4.4 SCROLL: the terminal's scrollback

User decision §9.3: scrolling is by vertical swipe, not by rim scrub. A drag in the centre moves line by line with detents; a flick (release velocity above the swipe threshold) moves a page.

```
              ╭──────────────────────────────────────╮
           ╭──╯            webapp · kitty              ╰──╮   16 px header: session · terminal kind
         ╭─╯                                            ╰─╮
        ╭╯                     ▲                          ╰╮  chevron lights in the direction moved
        │                                                  │
        │                   −142 lines                     │  32 px: lines above the bottom (0 = live)
        │                                                  │
        │   $ npm test -- --watch=false                    │  20 px muted, 1 line: top visible line of
        │                                                  │  the terminal (privacy ≥ preview only)
        │                     ▼                            │
        ╰╮                                                ╭╯
         ╰─╮  [A] TO BOTTOM                PAGE ▾ [B]  ╭─╯
           ╰──╮   drag to scroll · flick for a page  ╭──╯
              ╰──────────────────────────────────────╯
```

- The index ring becomes a position track: a 6 px grey ring with a sky-blue arc whose length is the fraction of the scrollback above the bottom (from `scroll_pos`, §6).
- Rate: a drag emits one step per 24 px = 3 lines (drag up = older, like dragging the page); a flick = 1 page; B = page down, swipe up = page up. The device coalesces steps into one `cmd_op_scroll` per 50 ms with the summed delta, so at most 20 commands per second cross the link; the BLE connection interval drops to the interactive setting while in SCROLL.
- Haptics: a 15 ms detent per step, a 40 ms stop when `scroll_pos.at_bottom` (or top) is reported.
- Preview (user decision §9.8): the daemon sends the terminal's top visible line, ≤ 48 chars, only when the device's `privacy` setting is `preview` or `full`; under `meta` the row is blank. It is terminal content, so it is never written to the audit log or the trace at `info`.
- Entering: only from the AGENT wheel ("Scroll terminal"), which first sends `cmd_op_focus` so the terminal is frontmost, then enters SCROLL. Leaving: tap centre (keeps the position), A (sends `to_end` bottom, exits), B long, chord, 8 s idle, or any new attention (which opens ASK).
- Only Working, Input and Error sessions offer it; a session the daemon reports as `unsupported` hides the item after the first failure.

### 4.5 STATUS

```
        ◦ BLE · dev-mac            16 px: transport · host      (amber "Offline · stale 4m" when down)
          82 %  ▮▮▮▮▯              battery, charging glyph
     ▶    Quiet until 14:30   [A]  rows are a wheel: scrub/B moves, A or tap commits
          Sounds: on
          3 sessions · 1 waiting   counts
          [A] QUIET 30 MIN         NEXT [B]
```

Reached by swipe down from HOME, the "Watch…" item, and shown as HOME when there are no sessions ("No sessions · waiting for an agent" replaces the counts). **Quiet mode lives only here** (user decision §9.7).

### 4.6 OFF and glow

Unchanged intent, two fixes carried from `2702775`: the first touch after a glow or a dark display only wakes (`power_activity()` returns true while `s_glowing`), and the glow colour follows the attention kind. Lifting during a glow (IMU, later) opens ASK directly.

## 5. Haptic vocabulary

ERM motor, 1 ms resolution, 35 ms floor for a felt pulse, 15 ms kick for a detent only. Distinguish by **count and rhythm, never by 10 ms differences**; gaps ≥ 60 ms; at most 3 pulses outside ramps. The approve/deny/reject triple is already in `feedback.c` (`2702775`).

| Event | Pattern (on/off ms) | Shape | Where |
|---|---|---|---|
| Detent (session, item, scroll step) | 15 | click | scrub, B, drag |
| Select / tick (focus sent, item picked) | 35 | one short | HOME, AGENT |
| Permission arrived | 250, 120, 250 | two long | glow, ASK |
| Question arrived | 40, 90, 40, 90, 40 | three short | glow, ASK |
| Error arrived | 500 | one long | glow |
| Done arrived | 60, 80, 60 | two short | glow |
| Approve ramp (during hold) | 35,170, 40,140, 45,110, 55,85, 65,65, 80,50, 100 | rising, gaps shrink | ASK |
| Deny ramp (during hold) | 100,50, 80,65, 65,85, 55,110 | falling | ASK |
| **Approved** (commit) | 150 | one firm long | ASK |
| **Denied** (commit) | 60, 90, 60 | two medium | ASK |
| Reply sent | 35, 60, 35, 60, 35 | three ticks | ASK |
| Reject / offline / nothing to do | 35, 70, 35, 70, 35 | three short | all |
| Scroll stop (at bottom/top) | 40 | one thud | SCROLL |
| Mode change (enter AGENT/SCROLL/STATUS) | 35 | one short | all |
| Quiet toggled | 35, 100, 35 | two spaced | STATUS |

Silent mode suppresses all but `hello`; quiet mode suppresses sound only (sound is still a stub).

## 6. Terminal scroll mode: protocol and daemon

### 6.1 Protocol (`protocol/agentlink.cddl`)

```
cmd_op_scroll = 10
feature_scroll = 7                 ; advertised by the daemon in welcome.feats

scroll_arg = {
  0 => scroll_unit,                ; unit
  1 => int,                        ; delta: negative = up (older), positive = down (newer)
  ? 2 => uint                      ; seq: device-side counter, so the daemon can drop stale bursts
}
scroll_unit = scroll_lines / scroll_pages / scroll_to_end
scroll_lines = 0
scroll_pages = 1
scroll_to_end = 2                  ; delta < 0 = top, > 0 = bottom (live)

result = {
  0 => bool,                       ; ok
  ? 1 => result_err,
  ? 2 => tstr,                     ; msg
  ? 3 => scroll_pos                ; new: only in answers to cmd_op_scroll
}
scroll_pos = {
  0 => uint,                       ; lines_above   0 = live
  1 => bool,                       ; at_bottom
  ? 2 => uint,                     ; total         scrollback size when known (for the track arc)
  ? 3 => tstr                      ; preview       top visible line, ≤ 48 chars, privacy ≥ preview only
}
```

`cmd` carries `op = 10`, `sid`, and `arg = scroll_arg`. The daemon answers at most one `result` per 250 ms per device (the last one wins) and only when the position changed or the command failed. Unknown ops are already dropped by old daemons, and the device hides the item after `result.err = unsupported`.

### 6.2 Daemon (`daemon/crates/agentlinkd/src/`)

- `device.rs handle_cmd()`: new arm `CmdOp::Scroll`. Look up the session and its `HostCtx` exactly as `Focus` does, then call `shared.scroll.scroll(&target, unit, delta)`. Coalesce: keep one pending delta per `(device, sid)`; if a new command arrives while the previous is executing, sum them. Trace one line per burst, not per command; nothing goes to the audit log (scroll is read-only on the PC).
- `focus/mod.rs`: a `ScrollDriver` trait beside `FocusDriver` with `scroll(ctx, unit, delta) -> Result<ScrollPos, FocusError>`, selected by the same `Outer` detection that focus uses. Scroll does **not** raise the window; the device already sent `focus` on entry.
- `focus/kitty.rs`: `kitty @ --to <listen_on> scroll-window --match <matcher> <amount>` with `<amount>` = `"{n}l-"` (lines up), `"{n}l+"`, `"{n}p-"` / `"{n}p+"` (pages), `"start"` / `"end"`. Position from `kitty @ ls` (`scrolled_by` per window); preview from `kitty @ get-text --match <matcher> --extent screen` first line, gated by the device's privacy setting. Requires the same remote-control setup focus already needs; the existing `NEEDS_REMOTE_CONTROL` message applies.
- `focus/tmux.rs`: if the pane is not in copy mode, `tmux copy-mode -t <pane>`; then `tmux send-keys -t <pane> -X -N <n> scroll-up|scroll-down` or `-X page-up|page-down`; `scroll_to_end` with `delta > 0` sends `-X cancel` (back to live), `delta < 0` sends `-X history-top`. Position via `display-message -p -t <pane> '#{scroll_position} #{history_size}'`; preview via `capture-pane -p -t <pane> -S <top> -E <top>`. The tmux layer runs **before** the outer-terminal layer: a tmux pane inside kitty scrolls tmux, not kitty.
- Terminal.app and Windows Terminal: no scroll API; return `Unsupported` (the device hides the item). Key injection (Shift+PgUp) is explicitly out of scope: it is the same hazard DESIGN §9 rejects for STT.
- Safety: `delta` is clamped to ±500 lines / ±20 pages per command, the drivers never pass device data through a shell, and the preview is cut at 48 chars and stripped of control characters before it leaves the daemon.

## 7. Reply and option selection

Sources of options, in order of precedence:

1. `attention.opts` (≤ 4 × 24 chars): Claude Code `AskUserQuestion` choices, Copilot `elicitation_dialog` choices. Shown verbatim; a 5th or longer option is the daemon's job to shorten or drop (it already enforces the limits). Commit by tap or A → `cmd_op_reply {rid, arg: index}`.
2. A permission with `acts` containing `approve`/`deny` but no `opts`: the device synthesises `Approve`, `Deny`, `Open on PC`. When the agent's prompt has "allow for this session / don't ask again" variants, the daemon sends them as `opts` with `kind = approval` and a new **required** `attention.grant_mask` (bit per option; set = the option grants something). Every set option is approve-class (hold); the reply is `cmd_op_reply {rid, arg: index}`, which the daemon maps to the agent's own choice. User decision §9.5: these grants are offered on the watch.
3. Error / Done attentions: no options; `acts` drive AGENT items (Dismiss, Acknowledge).

Flow for a question:

```
attention{kind=question, opts=["Yes","Yes, and run tests","No"]} ─► glow + 3 short
wake ─► ASK: title + text, wheel with 3 options + "Open on PC", default = first
scrub / B ─► detent per option      tap / A ─► cmd_op_reply{rid, arg=i} + "Sent" haptic
daemon ─► result ok ─► card line: Sent "Yes, and run tests" · waiting   ─► clear ─► HOME
      ─► result err  ─► toast with the error, options restored, reject haptic
```

Free-text replies are out of scope for v2 (voice in v3). "Open on PC" is always present so the user can escape to the terminal for anything the watch cannot express.

Daemon side: `handle_cmd` needs `CmdOp::Reply` wired to the approval broker that `Approve`/`Deny` are waiting on (today both return `Stale`). The broker maps `rid` → the pending hook response (Claude Code `PreToolUse`/`Notification`, Copilot `permissionRequest`/`elicitation_dialog`) and returns the option text or the permission decision through the hook's stdout, "first answer wins" per DESIGN §6a.

## 8. Migration from the Ring UI

### 8.1 Reuse as is (`firmware/components/core/src/`)

- `model.{c,h}`: sessions, attention queue (already parses `opts`), settings, counts. Add `grant_mask` to `attention_t`.
- `gesture.{c,h}`: rim scrub, buttons, chord, double-A, strict deny geometry. Add a vertical drag stream (`G_DRAG` with dy, emitted every 24 px in the centre) for SCROLL and a release-velocity flag on swipes for the page flick.
- `ui_ring.c`: becomes the index ring. Keep the segment model, per-segment invalidation and `ring_set_progress()`; shrink `BAND_W` to 6/12, replace the inner outline with a radial tick, inset the error dash, and add a "track" mode (one grey ring plus one coloured arc) for SCROLL.
- `ui_theme.{c,h}`: palette and fonts unchanged; add `theme_state_glyph()` per §4 and the new state words.
- `power.c`: keep, with the glow first-touch fix.
- `link_*`, `debug.c`, `app.c` action table: extend rather than replace.
- `feedback.c`: keep the structure; the approve/deny/reject triple is done, add the rest of §5.

### 8.2 Replace

- `ui_home.c` splits into: `ui_home.c` (deck: card + badges + link row), `ui_wheel.c` (the list widget: items, detents, hold tags, rim layout, used by AGENT, ASK and STATUS), `ui_ask.c` (prompt text + wheel + answer-pending line), `ui_scroll.c` (counter, preview, chevrons, coalescer). Each under 400 lines.
- `ui.c`: mode stack (HOME + one overlay mode), timeouts, toast placement moved above the key badges, `ui_report` gains `mode` and `item`.
- Edge hints become key badges `[A] WORD` / `WORD [B]` drawn at 7:30 and 4:30; they are not touch targets.

### 8.3 Build order

| Phase | Scope | Verifies |
|---|---|---|
| 0 | Review fixes #1–#4 (`2702775`): prompt text on the card, glow first-touch, haptic triple, strict deny | `approve_card`, `approve_after_glow`, `approve_too_soon`, `deny_short_swipe` |
| 1 | HOME deck: new hierarchy, summary row, time-in-state, index ring with tick and inset error dash, stale/offline, key badges, toast placement. Tap centre stays Focus until Phase 2 | screenshots of every state; `expect_label` on summary and stale |
| 2 | `ui_wheel.c` + AGENT for Working/Done/Error/Idle (Focus, Ack, Dismiss, Snooze, Notify, Watch…); tap centre and swipe up open it; chord = Home | `agent_wheel.txt` |
| 3 | ASK on the wheel: options from `opts`, `grant_mask`, `cmd_op_reply`, approve/grants/deny as hold items, answer-pending line | `ask_reply.txt`; protocol vector with `opts` |
| 4 | SCROLL by drag/flick + `cmd_op_scroll` + `scroll_pos` + kitty and tmux drivers + privacy-gated preview | `scroll_mode.txt`; daemon unit tests for argument building |
| 5 | STATUS wheel (swipe down from HOME, quiet mode); Atlas update (gesture map is generated from `ui_*.c`) | `status.txt` |
| 6 | IMU raise-to-wake and lift-during-glow (needs the BMI270 driver) | on-device only |

The simulator is the acceptance tool at each phase: scripts under `simulator/scripts/`, screenshots compared by eye, `expect_screen` extended with the mode name.

## 9. Decided (user answers, 2026-11-07)

1. **Tap centre opens the per-agent screen.** It is the wheel with gesture shortcuts layered on it (§4.2 explains why not a bare gesture pad). Swipe up does the same. Button A stays the eyes-free default action.
2. **Deny: both** the 0.8 s hold item and the strict swipe (≥ 120 px, starting above the centre).
3. **Scroll by vertical swipe/drag**, not by rim scrub. The rim is unused in SCROLL; session switching is horizontal or on the rim, so the two never meet (§3).
4. **Scroll entry only from the AGENT wheel.** Long-press centre on HOME is reserved (no action).
5. **"Don't ask again" grants are offered**, as hold items, gated by `attention.grant_mask`.
6. **Cancel agent: deferred** (no decision).
7. **A+B chord = Home.** Quiet mode lives only in STATUS (swipe down from HOME, or "Watch…").
8. **Scroll preview: yes**, one line ≤ 48 chars, only when privacy ≥ preview.
9. **Pebble-style `[A] WORD` key badges.**

## 10. Decisions that reconcile DESIGN.md and UI-RING.md

| Topic | DESIGN.md | UI-RING.md | **v2 decision** |
|---|---|---|---|
| State palette | §8 Primer (amber `#d29922`, red `#f85149`, …) | Okabe-Ito | **Okabe-Ito**; §8's state colours are superseded, its surfaces and text colours stay |
| Error haptic | 3 short | 1 long | **1 long** (500 ms) for the arrival; 3 short is the reject pattern |
| A+B chord | reserved | snooze all 30 min | **Home** (back to the deck); "snooze all" is a daemon feature, not a device one |
| Quiet-mode control | swipe-down quick settings on Glance | none | **STATUS screen**, reached by swipe down from HOME (so DESIGN's gesture survives) |
| Tap centre | focus (double-tap) | focus | **open the per-agent screen**; A = focus |
| Long-press centre | — | 600 ms "ring fills" = secondary | **reserved**; the fill is only ever a hold |
| Permission icon | — | hand / lock | **bell** now, `!`-in-circle glyph later; never a square |
| Elapsed time | — | elapsed | **time in state** (`tst`), 20 px, below the summary |
| Stale snapshot on link drop | "stale since hh:mm" | — | **`Offline · stale 4m`** plus dimmed centre, frozen elapsed |
| While-you-were-away list | §6a | — | deferred; the deck's auto-select of the most urgent session covers the common case |
| Attention card text | prompt preview + reveal tap | — | **always shown** (title + text); tap the text for `detail` |
| Double-tap | focus | retired | **retired**; tap and A are immediate |
| Button A default | focus | focus | **Focus** (Done also acks); on ASK A is "Open on PC", never approve |
| Segment order | by priority | pinned | **pinned** by creation; auto-select the most urgent |
| Terminal scroll | — | — | **new**: SCROLL mode by vertical drag/flick, `cmd_op_scroll`, kitty + tmux |
| IMU | lift/tilt wake | deferred | raise-to-wake only, once a driver exists; **never a commit input** |
