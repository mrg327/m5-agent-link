> **[UI-V3.md](UI-V3.md) supersedes this document's look, wording and back gesture** (swipe right goes back); the interaction model and safety rules below still stand.

# UI v2: "Deck": switch context, pick from a list, answer from the watch

Status: **decided; Phases 1–5 (HOME deck, AGENT wheel, ASK with the approval broker, SCROLL with the kitty and tmux drivers, STATUS with quiet mode) implemented in the simulator**. SCROLL left the wheel on 2026-11-09 (decision 27) and is entered by a drag on WATCH since 2026-11-10 (decision 29) (branch `ui-v2`, not yet validated on the device). Supersedes the Ring UI ([UI-RING.md](UI-RING.md)) and DESIGN.md §6, §6a (device side) and §8 (palette). Inputs: the Ring UX review (simulator pass, 2026-11-07), the user's v2 brief and answers (§9), and `docs/research/{stopwatch-interaction-capabilities,wearable-glance-gestures,minimal-peripherals}.md`.

Where this document and DESIGN.md or UI-RING.md disagree, this document wins. §10 lists each decision.

## 1. Principles

The watch answers one question per glance and one gesture per intent. **Context switching is the navigation**: the home screen is one session at a time, and the only thing you do on it is change which session (rim scrub, button B, side swipe) or open that session's own screen. Everything else is a **short list you move through with detents and commit from the centre**: the agent's actions, the agent's own answer options, the watch's settings. Lists are the one UI element, so the same thumb motion (scrub to move, tap to pick, hold to commit something irreversible) works everywhere, and the centre's vertical swipe means exactly one thing per mode. Safety is structural, not cosmetic: approvals show what is being approved, a tap never approves or denies, destructive commits are holds bound to a request id with a distinct haptic, and the first touch after a wake only wakes. Pixels are mostly off (AMOLED), motion is reserved for "needs you", and colour is never the only cue.

## 2. Navigation model

Six modes, one screen each. HOME is the only place you can stay, except WATCH, which stays until you leave it. Every other mode returns to HOME on commit, on **B long**, on the **A+B chord** (Home), or on timeout.

```
                 lift / button / touch-wake              attention arrives
   ┌─────┐ ──────────────────────────────► ┌──────────┐ ───────────────────► ┌──────────┐
   │ OFF │ ◄──────── 10 s idle ─────────── │   HOME   │ ◄── commit/back ──── │   ASK    │
   │glow │   (glow 2 s on attention,       │  (deck)  │                      │ card+list│
   └─────┘    first touch = wake only)     └──────────┘                      └──────────┘
                                          │  ▲    │  ▲
                        tap centre /      │  │    │  │ swipe down
                        swipe up          ▼  │    ▼  │ ("Status…" item too)
                                     ┌─────────┐  ┌──────────┐
                                     │  AGENT  │  │  STATUS  │  link · battery · quiet mode
                                     │ (wheel) │  └──────────┘
                                     └─────────┘
                                          │  "Watch" item opens
                                          ▼
                                     ┌──────────┐  drag   ┌──────────┐
                                     │  WATCH   │ ──────► │  SCROLL  │  the terminal moves with the finger
                                     └──────────┘ ◄────── └──────────┘
                                                   tap / B held / 8 s
                                     ┌──────────┐
                                     │  WATCH   │  live steps of one session; stays until you leave
                                     └──────────┘
```

- **HOME (deck).** One session card in the centre, a thin pinned index ring at the rim. Scrub, B or a side swipe changes the session. Button A does the state's default action (Focus). Tap centre or swipe up opens AGENT.
- **AGENT.** The per-agent screen (user decision §9.1): a curved list ("wheel") of that session's actions, default item preselected, with gesture shortcuts layered on it. Scrub moves, tap commits a safe item, hold commits a destructive one.
- **ASK.** Opened automatically when an attention (question, permission) arrives while the display is on, or on the first touch during a glow. It is AGENT with the agent's prompt text above the wheel and the agent's options as items. Done/error attentions do not open ASK; they update the card and buzz.
- **STATUS.** Link, host, battery, quiet mode, sounds. Swipe down from HOME; the empty deck points at it (§4.5).
- **SCROLL.** Vertical drags drive the watched session's terminal scrollback. Entered only by a drag on WATCH; it returns to WATCH (§4.4).
- **WATCH.** A live monitor for one session: its current step, the steps before it, the step count and the time. Opened by the "Watch" item; B or the rim moves to the next session (§4.7).

Timeouts: AGENT 8 s, ASK none (it waits for the answer or a `clear`), SCROLL 8 s after the last drag (back to WATCH), STATUS 8 s, WATCH none. The display turns off 10 s after the last input on HOME unless an unanswered attention is showing.

## 3. Gesture map

One table, every input × mode. "—" means ignored (and logged). Thresholds: long-press 600 ms, hold ≥ 1200 ms (daemon floor 1000 ms), swipe ≥ 60 px within 400 ms, **deny swipe ≥ 120 px starting above the centre line**, rim annulus r 160–260 px, centre r < 150 px, scrub detent per item.

| Input | OFF / glow | HOME (deck) | AGENT (wheel) | ASK (card + wheel) | SCROLL | STATUS | WATCH |
|---|---|---|---|---|---|---|---|
| First touch after off/glow | wake only (glow: reveal ASK) | | | | | | |
| Tap centre | wake | open AGENT (open ASK when a permission or question is pending) | commit selected item (safe items; a destructive item shows "hold") | commit selected item if safe (Open on PC, a question option); else shows "hold" | back to WATCH, keep terminal position (a tap that stops a glide only stops it) | commit selected row (nothing here is destructive) | open the session in the terminal (ASK when a request is pending) |
| Long-press centre (600 ms) | wake | — (reserved) | — | — | — | — | back to HOME |
| Hold centre ≥ 1.2 s (arc + ramp) | — | — | commit a destructive item | **Approve**-class item (bound to `rid`), or Deny (0.8 s); arc colour = item colour | — | — | — |
| Rim scrub CW / CCW | wake | next / previous session (detent) | next / previous item (detent) | next / previous option (detent) | — | next / previous row | next / previous session |
| Tap rim | wake | select the session under the finger | select the item at that angle (items are laid out on the ring) | same | — | — | watch the session under the finger |
| Vertical drag in centre | wake | — | — | — | trackpad: 1 line per 10 px past the tap slop, a detent per 3 lines; a fast release glides on | — | opens SCROLL on the watched session and scrolls (issue 5cbb) |
| Swipe up (centre) | wake | open AGENT (ASK when a request is pending) | — | — | its drag lines, then the glide (never a page) | back to HOME | its drag scrolls (SCROLL), it no longer leaves |
| Swipe down (centre) | wake | open STATUS | back to HOME | **Deny** (permission only, strict) | its drag lines, then the glide (never a page, deny or back) | — | — |
| Swipe left / right | wake | next / previous session | — | — | — | — | — |
| Button A short | wake | default action (Focus; Done also acks) | commit selected item (safe only) | **Open on PC** (focus), whatever is selected: A never approves | to bottom (live), back to WATCH | commit row (Quiet: off → 30 min → 1 h → off; Sounds: on/off; information rows: nothing) | focus the terminal |
| Button A double | wake | jump to next attention (Input › Error › Done-unseen) | — | — | — | — | — |
| Button A long | wake | — | — | — | — | — | — |
| Button B short | wake | next session | next item | next option | page down (newer) | next row | next session |
| Button B long | wake | previous session | back to HOME | back to HOME (request stays pending) | back to WATCH | back to HOME | back to HOME |
| A+B chord | wake | — (already home) | **Home** | **Home** (request stays pending) | **Home** | **Home** | **Home** |
| Lift (IMU, later) | wake; during a glow opens ASK | — | — | — | — | — | — |
| Shake (IMU) | — | — | — | — | — | — | — |

Why this is conflict-free:
- The centre and the rim are disjoint hit regions (r < 150 vs r ≥ 160); a press between them is a rim press. A scrub that starts in the annulus never becomes a swipe; a swipe or drag that starts in the centre never becomes a scrub.
- **The centre's vertical motion has one meaning per mode:** HOME opens things (up = AGENT, down = STATUS), AGENT/ASK go back or deny (down only), STATUS goes back (up only, the reverse of the swipe that opened it; down is ignored there). Session switching is horizontal (left/right) or on the rim, so it can never be mistaken for a deny or a scroll drag. Deny exists only in ASK, and only as the strict swipe or the hold item.
- The wheel is moved by the rim and B, never by a vertical drag, so the wheel and the deny swipe cannot overlap.
- Holds only start in modes where the selected item is destructive. On HOME nothing is destructive, so no centre press can approve anything.
- IMU is never a commit input (research: no trusted false-positive data). Shake is reserved.

## 4. Screens

Display 466 × 466, centre (233, 233). Fonts: Montserrat 16/20/24/32 (existing). Palette: Okabe-Ito for state, Primer-dark surfaces (§10). Every state has colour, a word, and a glyph.

| State | Colour | Glyph (centre) | Index-ring motion | Word |
|---|---|---|---|---|
| Working | sky `#56B4E9` | `LV_SYMBOL_REFRESH` | slow comet | Working |
| Needs input | yellow `#F0E442` | `?` | pulse 1 Hz | Question |
| Permission | orange `#E69F00` | `LV_SYMBOL_BELL` until a custom `!`-in-circle glyph exists | pulse 1 Hz, **two thin round tracks** | Approve? |
| Done | green `#009E73` | `LV_SYMBOL_OK` | one sweep, then dim after ack | Done |
| Error | vermillion `#D55E00` | `LV_SYMBOL_WARNING` | **a row of round dots**; one fast flicker, then solid | Error |
| Idle | grey `#999999` | `LV_SYMBOL_PAUSE` | none | Idle |

### 4.1 HOME: the deck

Glanceable: *which* session, *what state*, *what it just did*. One gesture deeper: everything else.

```
                     ╭──────────────────────────────────────╮
                  ╭──╯  ▬▬  ▬▬▬▬  ▬▬  ▬▬▬▬▬  ▬▬  ▬▬▬  ▬▬  ╰──╮    index ring, 6 px, pinned order,
                ╭─╯                                          ╰─╮  selected = 14 px + underline
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

- **The index ring is a ring of capsules.** Each session is an 8 px round-capped arc at the rim (outer radius 226), pinned by creation order from 12 o'clock, with 9° gaps so the capsules read as separate beads. **Selection** is a 14 px capsule grown both ways around the same centre line plus a 2 px white underline just inside it; the other capsules recede to 70 % brightness but keep their hue, so error and permission stay urgent. The underline is what survives ambient dim. At ≥ 9 sessions, acknowledged Done sessions merge into one dim capsule.
- **Round everywhere else:** the key caps are circles, the action pill is a filled 20 % tint of the state colour with no border, the toast is a tinted pill with white text, and the hold-to-approve arc has round caps.
- *Decision note (2026-11-07):* the capsule language was chosen from a round-direction review (capsules vs. orbit beads vs. soft track, with the `7c70ca4` look as baseline); the filled pill and tinted toast were taken from the soft-track direction. It replaced the earlier radial white tick, which was visible on the glass but weak.
- The state row is the largest coloured element; elapsed time is demoted to 20 px and always means "time in this state" (`tst`).
- The summary row shows `session.summary` for Working/Done/Error and `attention.title` + `text` for Question/Permission, so the approval text is already on the glanceable screen (carried from `2702775`).
- **Remote sessions** (`session.host` ≠ 0, REMOTE.md §3.3) lead their 16 px muted line with the host name: `WIN-BUILD01 · Copilot CLI · main` on the HOME card, `WIN-BUILD01 · Copilot CLI · webapp` in the AGENT, ASK and SCROLL headers (`theme_host_prefix()`). The host comes first so the ellipsis cuts the branch or folder, never the host; a remote session whose daemon sent no `hn` reads `Remote`. Local sessions (host 0) are drawn exactly as before.
- Key badges `[A] WORD` / `WORD [B]` sit at 7:30 and 4:30 beside the physical buttons. They are legends, not touch targets; a tap there is a rim tap.
- Offline: the centre drops to 50 % opacity, the badges grey out, elapsed freezes, and the link row reads `Offline · stale 4m`.
- Ambient (30 s idle, nothing needs you): ring 20 %, centre 30 %, pixel shift 1 px per minute. Carried over.
- Empty: the centre reads `No sessions` / `Waiting for an agent` with the hint `swipe down for status` (decision 23: STATUS is not embedded, since its idle timeout would fight an always-empty deck).

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
        │         ▶          Watch            [A] ◀        │  28 px white, state-coloured chevrons, shortcut tag
        │                                                  │
        │                Notify when done                  │  20 px muted (next)
        │                                                  │
        ╰╮                                                ╭╯
         ╰─╮  [A] SELECT                     NEXT [B]  ╭─╯
           ╰──╮                                      ╭──╯
              ╰──────────────────────────────────────╯
```

- At most 3 items (decision 27), the selected one in the centre row; the list wraps. Scrub or B moves it with a detent per item. Items are also laid out as arcs on the index ring while AGENT is open (one arc per item, selected = thicker), so a rim tap picks directly.
- Destructive items carry a trailing `HOLD` tag and commit only with a 1.2 s hold; a tap on them shows a 1 s hint "hold to confirm" and does nothing else.
- Items per state (default first):

| State | Items (default first) | Destructive (hold) |
|---|---|---|
| Working | Watch · Notify when done · Snooze 30 min · *Interrupt* | Interrupt |
| Question, Permission | *ASK opens instead (§4.3)* | Approve, grants, Deny (in ASK) |
| Done | Watch · *Reply…* | — |
| Error | Watch · Dismiss error · *Reply…* | — |
| Idle | Watch · *Reply…* · Snooze 30 min | — |
| Input (the session waits, no request listed) | Watch · *Reply…* · Focus terminal | — |

Nothing that has another path takes a row (decision 27, 2026-11-09): **Focus** is button A on HOME and WATCH (on a Done session it also acknowledges), **Status** is the swipe down on HOME. A session that waits for input keeps Focus.

Items in *italics* need a daemon that types into terminals (decision 28: the welcome carries `replies`, proto 0.4):
- **Reply…** turns the wheel into the reply list (header `Reply · <repo>`, items in the answer colour). When the agent's turn ended on a question (proto 0.5), the question shows above the list in 20 px and its answers come first (`Yes, go ahead` / `No`, or the options of an "A or B?" question); the user's own `~/.agentlink/replies.txt` entries follow. There are no built-in replies, so without a question or a file the item is hidden (decision 30). Tap or A sends `cmd_op_type` with the reply's index; the daemon types it and Enter. The toast reads `Sent "Continue"`. The device never sends text. The daemon refuses (toast with its reason) while the session has an open request, waits on an approval or question, or shows a numbered selection list.
- **Queue reply** (Working, daemon proto ≥ 0.17, PROTOCOL §6d) opens the reply list in queue mode: the fixed replies and Dictate, without Keyboard or the session's own answers (header `Queue for <repo>`). A pick sends `type` with `q: true`; Dictate opens VOICE whose hold button reads **Queue** and sends `dictate` with `q: true` (toast "Queued"). While the session holds a queued message (`qd`), the screen shows `Queued: <qd>` under the gauge readout and every state adds **Cancel queued** (`unqueue`).
- **Interrupt** is a hold item (deny colour, `HOLD` tag, 1.2 s): it sends `cmd_op_cancel` with the measured `hold_ms`, and the daemon sends Esc to the terminal. A tap only says "Hold to confirm".

- **Watch** opens the WATCH monitor for this session (§4.7).
- **Dismiss error** (Error) sends `ack`; so does opening the wheel on a Done session that is not yet seen (decision 32). The daemon stores the result: it sets `seen`, tells every device, and moves an acknowledged error session to idle. The device only dims the segment at once.
- **Snooze 30 min** and **Notify when done** are toggles. While the session is snoozed (`snz` is in the future) the item reads **Unsnooze** and sends `snooze` with 0 minutes (toast "Unsnoozed"). While `ntf` is set, the item reads **Don't notify** and sends `notify_done` with 0 (toast "Won't notify"). Arming sends 1.
- A snoozed session buzzes, glows and nags for nothing. Its card still appears, and its ring segment is dimmed. The card shows "snoozed" after the time.

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

- The prompt text is the top half of the screen; the options are the wheel in the bottom half (three rows, the selected one at y 272). Nothing is hidden behind a gesture. The `HOLD` tag has a fixed slot at the right end of the selected row, in place of the right chevron, so every hold item reads the same whatever its label length.
- Order: `attention.opts` verbatim with the first option selected (the agent's own default); a bare permission shows Approve · Deny; "Open on PC" is always the last item. In a permission, an option that does not grant (the agent's "No") is a deny-class hold (0.8 s), never a tap.
- Colour says what an item does, never alone: orange grants, vermillion denies, yellow answers a question, sky blue goes to the PC; the arc and the haptic ramp follow the item.
- Opening ASK: a permission or question that arrives while the display is on opens it directly; during a glow the first touch reveals it; on the deck a tap, double tap or swipe up on a request's card opens it (the pill reads `TAP › ANSWER`). The deck itself has no hold: nothing on HOME can approve.
- **Approve-class options (anything that grants permission, including "don't ask again" variants, and `Always allow` with a longer 1.6 s hold, [STANDING-RULES.md](STANDING-RULES.md)) commit only by hold.** The hold starts after 250 ms, and only once the card has been on screen 300 ms (`CARD_ARM_MS`); it fills a 10 px arc in the item's colour from 12 o'clock over the index ring, with the escalating haptic ramp; early release cancels; the hold is bound to `rid` and cancels if the attention changes or is cleared.
- Deny commits by hold (0.8 s, falling haptic) **or** by the strict swipe down (≥ 120 px, starting above the centre line). Both answer with `cmd_op_deny`.
- Question options (`attention.opts`, ≤ 4 × 24 chars) commit by tap or A; they are not destructive. The reply is `cmd_op_reply` with `arg = option index`.
- Toasts on ASK (`Hold to confirm`, `Out of date`, `Offline`) take the hint line under the key badges (`UI_TOAST_LOW_Y`; the hint hides meanwhile), not the action-pill slot the deck uses, which would sit on the wheel's selected row. STATUS does the same.
- After an answer: the wheel collapses to one line `Approved · waiting for PC` (or `Denied`, `Sent "Yes" · waiting for PC`) in the answer's colour (green, vermillion, yellow); the index ring shows one dim capsule; the card stays until `clear`, then HOME returns. A `result.ok = false` shows the error as a toast, restores the options and buzzes the reject pattern.
- Nag: up to 3 re-buzzes every 15 s while a permission is unanswered and the display is off; any input stops it. (2026-11-12: spread over the answer window, at least 15 s apart, none in its last 5 s: [ANSWER-WINDOW.md](ANSWER-WINDOW.md) §1. A request the daemon marks `hush` (the user is looking at its terminal, [PRESENCE.md](PRESENCE.md)) is not nagged, and does not buzz or glow.)

### 4.4 SCROLL: the terminal's scrollback

User decision §9.3: scrolling is by vertical swipe, not by rim scrub. The centre behaves like a trackpad or a scroll wheel: the terminal moves with the finger, at once, and keeps gliding after a fast release.

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
           ╰──╮        drag like a trackpad          ╭──╯
              ╰──────────────────────────────────────╯
```

- The index ring becomes a position track: a 6 px grey ring with a sky-blue arc whose length is the fraction of the scrollback above the bottom (from `scroll_pos`, §6).
- Trackpad decision, 2026-11-09 (replaces the Phase 4 step-and-flick rate): the old 24 px = 3 lines steps, the 50 ms wait before the first command and the flick-is-a-page felt like "enter and send". Now:
  - Rate: one step per 10 px of vertical travel = 1 line, the unit the daemon turns into one physical wheel line (drag up = older, like dragging the page). The stream starts only once the finger leaves the 18 px tap slop, counted from that point, so a tap never scrolls.
  - Leading edge: the first step on an idle link goes out at once; steps within the next 40 ms are summed into one `cmd_op_scroll`, so at most 25 commands per second cross the link (`NAV_SCROLL_COALESCE_MS`). While SCROLL is open the device asks for a 15–30 ms BLE connection interval (`hal_link_interactive`; Apple's floor is 15 ms) and returns to the central's own interval when it closes.
  - Glide: the lift after a drag reports its vertical speed over the last 80 ms (`G_DRAG_END`, any drag length). From 300 px/s (capped at 3000) the view keeps scrolling through the same path, decaying with a 400 ms time constant, until it falls under 60 px/s or after 2.5 s. A new touch anywhere, A or B, leaving SCROLL, the live bottom (scrolling newer) or a reported top (scrolling older) stops it; gliding counts as activity for the 8 s timeout. A tap that stops a glide only stops it.
  - A flick is no longer a page; B is the only page (newer), A the jump to the bottom.
- Counter: `Live` / `at the bottom` when `lines_above` is 0, else `-N lines` / `above the bottom`. The device predicts the counter from the lines it sent and the daemon's `scroll_pos` corrects it; pages are not predicted (the page height is the terminal's).
- Haptics: a 15 ms detent per 3 lines the finger moves (one wheel notch), none while gliding; a 40 ms stop when `scroll_pos.at_bottom` (or top) is reported.
- Preview (user decision §9.8): the daemon sends the terminal's top visible line, ≤ 48 chars, only when the device's `privacy` setting is `preview` or `full`; under `meta` the row is blank. It is terminal content, so it is never written to the audit log or the trace at `info`.
- Entering (issue 5cbb, 2026-11-10): a vertical drag in the centre of WATCH. It first sends `cmd_op_focus` so the terminal is frontmost (quietly: no "Opening on PC" or "Focused" toast while SCROLL is up), then opens SCROLL over WATCH, and that first drag step already moves the terminal. Leaving: tap centre (keeps the position), A (sends `to_end` bottom), B long or 8 s idle return to WATCH; the A+B chord goes to HOME; any new attention opens ASK. The rim is ignored, and in SCROLL a swipe down is only scrolling, never deny or back (§3). There is no wheel item (decision 27).
- A session the daemon reports as `unsupported` is marked (`session_t.no_scroll`, device-side, reset by a snapshot): SCROLL closes back to WATCH with the "Not supported" toast, and later drags on that session's WATCH only repeat the toast.

### 4.5 STATUS

```
              ╭──────────────────────────────────────╮
           ╭──╯        ◦ BLE · dev-mac                ╰──╮  16 px: transport · host (amber "Offline · stale 4m" when down)
         ╭─╯            82 %  ▬▬▬▬▬▬▬▬▬▬▭▭             ╰─╮  24 px percent beside a round-capped gauge; ⚡ while charging
        ╭╯            ───────────────────               ╰╮  capsule divider
        │               3 sessions · 1 waiting            │  rows are a wheel: scrub/B moves, A or tap commits
        │               Firmware 0.4.2                    │
        │          ▶    Quiet: off                 ◀      │  the default row; "Quiet until 14:30" while it runs
        │               Sounds: on                        │
        │               Privacy: preview                  │
        ╰╮                                                ╭╯
         ╰─╮  [A] QUIET 30 MIN                 NEXT [B] ╭─╯  the A word names what the commit will do
           ╰──╮        swipe up to go back           ╭──╯
              ╰──────────────────────────────────────╯
```

Reached by swipe down from HOME and by the "Status…" item on the AGENT wheel; the empty deck says `swipe down for status`. The index ring keeps showing the sessions, so the deck's glance survives the visit. **Quiet mode lives only here** (user decision §9.7). Rows, in wheel order (`nav_status_items()`):

| Row | Text | Commit (A or tap) |
|---|---|---|
| Quiet | `Quiet: off` · `Quiet until 14:30` (local time; `Quiet: on` before the clock is set) | cycles `settings.quiet_until`: off → now + 30 min → now + 1 h → off (`nav_quiet_next()`); haptic "quiet toggled" (§5); the A badge reads `QUIET 30 MIN` / `QUIET 1 H` / `QUIET OFF` |
| Sounds | `Sounds: on` · `Sounds: off` · `Silent` | toggles the alert mode full ↔ quiet (sound off); silent is only set by the daemon's `cfg` |
| Privacy | `Privacy: meta` / `preview` / `full` | nothing (set on the PC); the A badge hides |
| Sessions | `3 sessions · 1 waiting` (or `· 1 error`, `No sessions`) | nothing |
| Firmware | `Firmware 0.4.2` (`hal_board_info()->fw_version`) | nothing |

While quiet runs (`model_quiet_active()`), an attention that arrives neither buzzes, glows nor nags (`app.c`); the card and the ring still change, and a permission or question still opens ASK when the display is already on. Ticks and detents (feedback for the user's own input) are unaffected. The deck shows quiet subtly: a mute glyph leads the link row. There is no toast on STATUS: the row's new text and the haptic are the confirmation, so the rows stay readable. Leaving: swipe up, B long, chord, or 8 s idle. Quiet is device-side only for now (decision 19); a `cfg` from the daemon still overrides `quiet_until` and the alert mode.

### 4.6 OFF and glow

Unchanged intent, two fixes carried from `2702775`: the first touch after a glow or a dark display only wakes (`power_activity()` returns true while `s_glowing`), and the glow colour follows the attention kind. Lifting during a glow (IMU, later) opens ASK directly.

### 4.7 WATCH

A live monitor for one running session on the small screen. Opened by the "Watch" item (§4.2). The watched session is the deck's selection: B and the rim move the deck, and the watch follows. Only one session is watched at a time, and only while this screen is open.

```
              ╭──────────────────────────────────────╮
           ╭──╯                                      ╰──╮
         ╭─╯           Claude Code · webapp            ╰─╮   16 px header: agent · repo (host name first when remote)
        ╭╯                                                ╰╮
        │            ✓ Read · src/main.c                   │  16 px muted rows: up to 3 earlier steps, oldest on top
        │            ✓ Edit · src/app.c                    │  mark: green check = ok, red cross = failed, accent play = running
        │                                                  │
        │                      Bash                        │  24 px: the current tool
        │            npm test -- --watch=false             │  20 px muted: its argument, up to 2 lines, ellipsized
        │                                                  │
        │                3 steps · 1:37                    │  20 px muted footer
        ╰╮  [A] FOCUS                          NEXT [B]  ╭─╯
         ╰─╮    drag to scroll • hold to go back       ╭─╯
           ╰──╮                                      ╭──╯
              ╰──────────────────────────────────────╯
```

- The rim keeps the index ring, with the watched session selected in its state colour.
- The step comes from `sum` ("Tool · argument"), else `tool`. The timeline keeps the last four steps per session (`steps.c`): a new `n` pushes a running step, `ok` finishes step `n`, and a smaller `n` or 0 starts a new turn and clears it. The screen redraws on every delta for that session, and the clock once a second.
- Footer: `<n> steps · <m:ss>`. The time is the time in the current state (`tst`, else `t0`) and shows only while the session works. The other states show the step count alone.
- When the session is not working, the centre shows the state instead: Done `Finished · 3m ago`, Error `Error`, Input `Waiting for you` with `tap to answer`, Idle `Idle`.
- No timeout: WATCH stays until you leave. The display still turns off after 10 s idle, and the wrist raise wakes it on the same screen.
- If the watched session ends or is removed, the device goes back to HOME with the toast "Session ended".
- An attention for any session behaves as on HOME and may open ASK. When ASK closes (answered and cleared, B long, a question's swipe down, Open on PC), the device returns to WATCH on the session it was watching, if that session is still there; the A+B chord in ASK goes to HOME and watching stops. Checked by `watch_ask_return.txt`.
- A Done or Error transition of the watched session buzzes the finished pattern (§5), unless it is snoozed or quiet mode runs.

| Input | Action |
|---|---|
| Tap centre | focus the terminal (opens ASK when the session waits for an answer) |
| Button A | focus the terminal |
| Button B / rim scrub / tap rim | next / previous session; the watch follows |
| Vertical drag in the centre | scroll the terminal: opens SCROLL over WATCH (§4.4) |
| Hold a finger still on the centre (600 ms), B long, A+B chord | back to HOME, watching stops |
| Swipe left, right | ignored |

### 4.8 VOICE (issue 0ea9)

Dictate a reply to one session. "Dictate…" ends the Reply list when the daemon can transcribe (`welcome.voice`).

- **Listening:** `Listening…`, the elapsed time (48 px), the loudness of the last 100 ms as the ring's arc, hint `tap to stop`. Tap or A stops; it stops by itself at 60 s (15 s before issue f97a). The display stays on.
- **Transcribing:** `Transcribing…` until the transcript arrives (45 s at most, then `No transcript`).
- **Transcript:** the text (20 px, up to 5 lines), hint `hold to send`. Only the 1.2 s hold sends it (`cmd dictate`, the approve ramp and arc in the answer colour; armed 300 ms after the text appears); a tap says "Hold to send". The toast reads `Sent`.
- **Failed:** the reason (`Nothing heard`, `Offline`, `No microphone`, `No transcript`), hint `tap to try again`.
- Long swipe down or B held discards, the A+B chord goes HOME, and an arriving request opens ASK; a recording left this way is never sent. The badges read A `STOP` / `HOLD` / `RETRY` and B `BACK`.

### 4.9 KEYS (issue 1e57)

A swipe keyboard for one session, for fine edits or when speaking is not possible. "Type…" ends the Reply list after "Dictate…" when the daemon decodes strokes (`welcome.keys`). The text itself is not shown on the watch: it appears in the terminal as you write.

- **Layout:** QWERTY in three rows of 44 × 54 px keys (y 170, 224, 278; 2026-11-10, up from 40 × 48, which cut the stroke decoder's top-1 misses slightly: 82.4 % vs 81.6 %), then a bottom row of layer switch, space and backspace (y 332). Each key corner sits at least 4 px inside the glass. The top cap above the keys holds the daemon's three best candidates as tappable labels, the typed one highlighted (a deviation from the plan's rim arcs: the cap is larger and easier to hit).
- **Writing:** draw one word per stroke through its letters; lift to finish. A short tap types a single letter. The trail of the current stroke is drawn on the keys.
- **Candidates:** tapping one replaces the last word (`kb_pick`). Backspace or B erases the last word (`kb_del`).
- **Sending:** A held 1.2 s presses Enter (`kb_send`, the approve ramp in the answer colour). The toast reads `Sent`.
- B held leaves and erases unsent text (`kb_end` 1); the A+B chord goes HOME and also erases; an arriving request opens ASK. Touch gestures (swipe, scrub) are off in this mode. The badges read A `SEND` and B `DEL`; they sit below the keys at radius 190 (199° / 161°) instead of the usual spot, which the keys now cover.
- A double tap below the keys (y ≥ 386, two taps within 400 ms) goes back like B held: it leaves and erases unsent text. A single tap there does nothing and is never sent as a stroke.
- **Device details:** the layer key (`cp` 0x0E, "123"/"abc") is local: it swaps in a digits-and-symbols layer (`1234567890`, `-_./:~=+@`, `#%&*?!'`) and resends `kb_start` with the same `kid`. Strokes are sampled at ≥ 30 ms, at most 200 points, `seq` from 0 per keyboard. A tap on a candidate sends `kb_pick`, never a stroke. Every exit sends `kb_end` with arg 1; the keyboard stays open after `kb_send`. Only three candidates are shown.

## 5. Haptic vocabulary

ERM motor, 1 ms resolution, 35 ms floor for a felt pulse, 15 ms kick for a detent only. Distinguish by **count and rhythm, never by 10 ms differences**; gaps ≥ 60 ms; at most 3 pulses outside ramps. The approve/deny/reject triple is already in `feedback.c` (`2702775`).

| Event | Pattern (on/off ms) | Shape | Where |
|---|---|---|---|
| Detent (session, item, 3 scroll lines) | 15 | click | scrub, B, drag (not the glide) |
| Select / tick (focus sent, item picked) | 35 | one short | HOME, AGENT |
| Permission arrived | 250, 120, 250 | two long | glow, ASK |
| Question arrived | 40, 90, 40, 90, 40 | three short | glow, ASK |
| Error arrived | 500 | one long | glow |
| Done arrived | 60, 80, 60 | two short | glow |
| Approve ramp (during hold) | 35,170, 40,140, 45,110, 55,85, 65,65, 80,50, 100 | rising, gaps shrink | ASK |
| Deny ramp (during hold) | 100,50, 80,65, 65,85, 55,110, 45 | falling, ends on a pulse | ASK |
| **Approved** (commit) | 150 | one firm long | ASK |
| **Denied** (commit) | 60, 90, 60 | two medium | ASK |
| Reply sent | 35, 60, 35, 60, 35 | three ticks | ASK |
| Reject / offline / nothing to do | 35, 70, 35, 70, 35 | three short | all |
| Scroll stop (at bottom/top) | 40 | one thud | SCROLL |
| Mode change (enter AGENT/SCROLL/STATUS/WATCH) | 35 | one short | all |
| Quiet toggled | 35, 100, 35 | two spaced | STATUS |
| **Finished: done** | 30, 200, 30 | two soft, long gap | a session moved into Done, when notify is armed or it is watched |
| **Finished: error** | 120, 90, 120, 90, 120 | three firm | a session moved into Error, same rule |

The finished buzz needs: the session just entered Done or Error, and (its `ntf` was set, or it is the watched session), and it is not snoozed, and quiet mode is not running. A snoozed session's attentions do not buzz, glow or nag either.

Three controls (§4.5): the alert mode `silent` suppresses every haptic but `hello`; the alert mode `quiet` ("Sounds: off") suppresses sound only (the speaker plays the sound bank's `done` pick when a session finishes and its `input` pick when an agent asks a question or needs approval, `core/src/sound.c`); the timed **Quiet** (`quiet_until`) suppresses the *alerts* (attention arrival, glow, nag) while leaving the detents, ticks and commit haptics, which answer the user's own input.

## 6. Terminal scroll mode: protocol and daemon

The device side is entered from WATCH since 2026-11-10 (§4.4, decision 29).

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

Phase 4 decisions: `scroll_arg` is the third member of `cmd_arg = uint / tstr / scroll_arg` (zcbor and the Rust `Arg` enum both take the map branch, so no other shape was needed). `feature_scroll = 7` exists in the `feature` enum and the device advertises it in `hello.caps.feats`; `welcome` has no `feats` key yet, so the device learns a daemon's lack of support from `result.err = unsupported` rather than up front. `scroll_pos` carries a `preview` only when the daemon's privacy for that link is `preview` or `full`, and the daemon writes that `result` to the trace with the preview replaced by `<private>` and without the raw bytes.

### 6.2 Daemon (`daemon/crates/agentlinkd/src/`)

- `device.rs handle_cmd()`: new arm `CmdOp::Scroll`. Look up the session and its `HostCtx` exactly as `Focus` does, then call `shared.scroll.scroll(&target, unit, delta)`. Coalesce: keep one pending delta per `(device, sid)`; if a new command arrives while the previous is executing, sum them. Trace one line per burst, not per command; nothing goes to the audit log (scroll is read-only on the PC).
- `focus/mod.rs`: a `ScrollDriver` trait beside `FocusDriver` with `scroll(ctx, unit, delta) -> Result<ScrollPos, FocusError>`, selected by the same `Outer` detection that focus uses. Scroll does **not** raise the window; the device already sent `focus` on entry.
- `focus/kitty.rs` (wheel decision, 2026-11-09): one `kitten @ --to <listen_on> kitten --match <matcher> <path> <unit> <delta> <end_notches> <preview>` per burst runs `focus/kitty_wheel.py` inside kitty and does exactly what kitty does for a physical wheel (kitty `mouse.c` `scroll_event`): a program that tracks the mouse gets SGR wheel buttons 4/5 (mode `mouse`); otherwise the main screen's history scrolls (`Screen.scroll`, mode `main`); otherwise a full-screen program on the alternate screen gets up/down arrow keys (mode `keys`). This matters because agent TUIs (Copilot CLI, Claude Code) run on the alternate screen, which has no scrollback: the old `scroll-window` call reached `lines_above 0` every time and nothing moved. One round trip measured 24–35 ms. Lines are paced inside kitty, like a trackpad's smooth scroll: an idle wheel moves the first line at once, and the rest of a batch is spread over the time until the watch's next batch (the measured gap between batches, 15–80 ms; 40 ms after a pause) by one kitty timer per window, at most one step per 8 ms. Batches in opposite directions cancel in the queue, a page or `to_end` first moves what is still queued, and on the main screen the reported `scrolled_by` is where the queue will leave it. Pages are `lines-1` lines (keys) or `(lines-1)/3` notches (mouse); `to_end` down on the alternate screen sends `end_notches` lines, up does nothing. The kitten is written to `~/.agentlink/kitty/agentlink_wheel.py` (dir 0700, file 0600, rewritten only when it changed). It reports `{mode, moved, scrolled_by, history, lines, preview}`: on the main screen the position is kitty's exact `scrolled_by`/`history` and the preview the top visible line; on the alternate screen the daemon counts the wheel lines it sent (`scroll::Tracked` under `<matcher>#alt`, reset by `to_end`) and the preview is the last non-blank line at or above the centre. If the kitten cannot run, the driver falls back to the old `kitty @ scroll-window` path (`"{n}l-"`, `"{n}p+"`, `"start"`/`"end"`, position from `ls`/`get-text --extent all`). Requires the same remote-control setup focus already needs; the existing `NEEDS_REMOTE_CONTROL` message applies.
- `focus/tmux.rs`: if the pane is not in copy mode, `tmux copy-mode -t <pane>`; then `tmux send-keys -t <pane> -X -N <n> scroll-up|scroll-down` or `-X -N <n> page-up|page-down`; `scroll_to_end` with `delta > 0` sends `-X cancel` (back to live), `delta < 0` sends `-X history-top`. Position via `display-message -p -t <pane> '#{pane_in_mode} #{scroll_position} #{history_size} #{pane_height}'`; preview via `capture-pane -p -t <pane> -S -<pos> -E -<pos>` (the top visible line). A move down that lands on position 0 sends `-X cancel`, so the pane leaves copy mode and the agent's output flows again, as tmux's own mouse wheel does; a move down while live is a no-op. The tmux layer runs **before** the outer-terminal layer: a tmux pane inside kitty scrolls tmux, not kitty.
- Terminal.app and Windows Terminal: no scroll API; return `Unsupported` (the device hides the item). Free key injection (Shift+PgUp, typed text) stays out of scope: it is the same hazard DESIGN §9 rejects for STT. The kitty driver's arrow keys are the one exception, and only where kitty itself turns a physical wheel into arrow keys (a full-screen program that does not track the mouse); they are fixed keys, never device data. Synthesized OS wheel events (macOS `CGEvent`, needs Accessibility) are a possible later route for these terminals.
- Safety: `delta` is clamped to ±500 lines / ±20 pages per command, the drivers never pass device data through a shell, and the preview is cut at 48 chars and stripped of control characters before it leaves the daemon.

## 7. Reply and option selection

Sources of options, in order of precedence:

1. `attention.opts` (≤ 4 × 24 chars): Claude Code `AskUserQuestion` choices, Copilot `elicitation_dialog` choices. Shown verbatim; a 5th or longer option is the daemon's job to shorten or drop (it already enforces the limits). Commit by tap or A → `cmd_op_reply {rid, arg: index}`.
2. A permission with `acts` containing `approve`/`deny` but no `opts`: the device synthesises `Approve`, `Deny`, `Open on PC`. When the agent's prompt has "allow for this session / don't ask again" variants, the daemon sends them as `opts` with `kind = approval` and a new **required** `attention.grant_mask` (bit per option; set = the option grants something). Every set option is approve-class (hold); the reply is `cmd_op_reply {rid, arg: index}`, which the daemon maps to the agent's own choice. User decision §9.5: these grants are offered on the watch. (2026-11-12, [STANDING-RULES.md](STANDING-RULES.md), proto 0.13: a Claude Code permission no longer has these options. Its "don't ask again" is `Always allow`, offered only for one of Claude Code's own `permission_suggestions` that the watch can show in full, with its rule and scope on screen before a 1.6 s hold that names the rule's hash; Copilot's is left to the terminal.)
3. Error / Done attentions: no options; `acts` drive AGENT items (Dismiss error; a Done session is acknowledged by opening its wheel).

Flow for a question:

```
attention{kind=question, opts=["Yes","Yes, and run tests","No"]} ─► glow + 3 short
wake ─► ASK: title + text, wheel with 3 options + "Open on PC", default = first
scrub / B ─► detent per option      tap / A ─► cmd_op_reply{rid, arg=i} + "Sent" haptic
daemon ─► result ok ─► card line: Sent "Yes, and run tests" · waiting   ─► clear ─► HOME
      ─► result err  ─► toast with the error, options restored, reject haptic
```

Free-text replies are out of scope for v2 (voice in v3). "Open on PC" is always present so the user can escape to the terminal for anything the watch cannot express.

Daemon side (Phase 3, `daemon/crates/agentlinkd/src/broker.rs`): the approval broker maps `rid` → the hook that is blocking for a decision. `agentlink-hook` blocks on Claude Code's `PermissionRequest` (the event that fires when the permission prompt is about to appear; output `hookSpecificOutput.decision.behavior`, with `updatedPermissions` = the one suggestion the watch showed and the user held `Always allow` for, STANDING-RULES.md; before 0.13 it was the whole `permission_suggestions` array for "Yes, don't ask again") and on `PreToolUse` for `AskUserQuestion` (output `permissionDecision: allow` + `updatedInput.answers`), for the daemon's answer window ([ANSWER-WINDOW.md](ANSWER-WINDOW.md): 60 s by default, `AGENTLINK_ANSWER_WINDOW_S` or `~/.agentlink/answer_window`, kept inside the hook's own Claude Code `timeout`; it was a fixed 15 s before 2026-11-12, and still is with an older daemon). Claude Code shows its dialog only after the hook returns, so the two are never live at once and the window is a trade against the user at the keyboard; Open on PC on the watch hands the request back at once (`handed_back`). `handle_cmd` routes `Approve`/`Deny`/`Reply` to `Broker::answer`; an unknown or settled `rid` is `stale`, a bad option index `denied`. "First answer wins" (DESIGN §6a): a device answer releases the hook and clears the device (`answered_dev`); the daemon's timeout, or a hook that stopped listening, clears it (`expired`) and the hook exits with **no output**, which Claude Code treats as "no decision" so its terminal prompt appears; any later hook event from the same session (the terminal answered) cancels what was pending (`answered_pc`). The hook never prints a decision it did not receive. Limits: one question with single-select options (multi-question or multi-select prompts are not offered to the watch: the hook returns no decision at once); Copilot has no decision path yet. (Proto 0.18: questions are held at their `PermissionRequest` and every shape of `AskUserQuestion` is answered on the watch, [LAST-MESSAGE.md](LAST-MESSAGE.md) §3.)

## 8. Migration from the Ring UI

### 8.1 Reuse as is (`firmware/components/core/src/`)

- `model.{c,h}`: sessions, attention queue (parses `opts` and `grant_mask`), settings, counts; `answer`/`answer_opt` record what went out so ASK can show the pending line and restore the options on a rejected command.
- `gesture.{c,h}`: rim scrub, buttons, chord, double-A, strict deny geometry. Add a vertical drag stream (`G_DRAG` with dy, every 10 px once the finger leaves the tap slop) for SCROLL, and `G_DRAG_END` with the release velocity `vy` that starts the glide (§4.4; the 2026-11-09 trackpad change replaced the 24 px steps and the page flick).
- `ui_ring.c`: becomes the index ring (done in Phase 1: capsules, selection underline, dotted error, double-track permission). Still to add: a "track" mode (one grey ring plus one coloured arc) for SCROLL.
- `ui_theme.{c,h}`: palette and fonts unchanged; add `theme_state_glyph()` per §4 and the new state words.
- `power.c`: keep, with the glow first-touch fix.
- `link_*`, `debug.c`, `app.c` action table: extend rather than replace.
- `feedback.c`: keep the structure; the approve/deny/reject triple is done, add the rest of §5.

### 8.2 Replace

- `ui_home.c` splits into: `ui_home.c` (deck: card + badges + link row; no hold), `ui_wheel.c` (the list widget: items, detents, hold tags, a `wheel_layout_t` for where the rows sit; used by AGENT, ASK and STATUS), `ui_ask.c` (prompt text + wheel + answer-pending line + the rid-bound hold; done in Phase 3), `ui_scroll.c` (counter, preview, chevrons, coalescer), `ui_status.c` (link and battery header, the rows, the quiet cycle; done in Phase 5). Each under 400 lines.
- `ui.c`: mode stack (HOME + one overlay mode), timeouts, toast placement moved above the key badges, `ui_report` gains `mode` and `item`.
- Edge hints become key badges `[A] WORD` / `WORD [B]` drawn at 7:30 and 4:30; they are not touch targets.

### 8.3 Build order

| Phase | Scope | Verifies |
|---|---|---|
| 0 | Review fixes #1–#4 (`2702775`): prompt text on the card, glow first-touch, haptic triple, strict deny | `approve_card`, `approve_after_glow`, `approve_too_soon`, `deny_short_swipe` |
| 1 | HOME deck: new hierarchy, summary row, time-in-state, index ring with tick and inset error dash, stale/offline, key badges, toast placement. Tap centre stays Focus until Phase 2 | screenshots of every state; `expect_label` on summary and stale |
| 2 | `ui_wheel.c` + AGENT for Working/Done/Error/Idle (Focus, Dismiss error, Snooze, Notify, Watch, Status…); tap centre and swipe up open it; chord = Home. **Done** (`nav.c` interaction layer, §8a) | `actions_wheel.txt`, `agent_timeout.txt` |
| 3 | ASK on the wheel: options from `opts`, `grant_mask`, `cmd_op_reply`, approve/grants/deny as hold items, answer-pending line; daemon approval broker + blocking Claude Code hooks. **Done** | `ask_reply`, `ask_grant_hold`, `ask_deny_hold`, `ask_tap_never_approves`, `ask_result_error`, `ask_hold_bound_to_rid`; vectors `attention_question_opts`, `attention_permission_grants`, `cmd_reply`; broker and hook unit tests |
| 4 | SCROLL by drag/flick + `cmd_op_scroll` + `scroll_pos` + kitty and tmux drivers + privacy-gated preview. **Done** (simulator and daemon unit tests; not yet run against a real kitty or tmux, and the BLE interactive interval is still untouched) | `scroll_mode.txt`, `scroll_exit.txt`, `scroll_privacy.txt`, `scroll_unsupported.txt`; `test_gesture` drag/flick cases; vectors `cmd_scroll`, `result_scroll_pos`; daemon tests for kitty/tmux argument building, position parsing, preview tidying, the coalescer and the link-level burst test |
| 5 | STATUS wheel (swipe down from HOME, "Status…"; quiet cycle, sounds toggle, privacy / counts / firmware rows; quiet holds the alert buzz, glow and nag; the deck's mute glyph); deny removed from the deck; Atlas facts (gesture map from `nav.c`, `status` screen, `quiet_toggled` haptic). **Done** (simulator) | `status.txt` (rows, cycle, every exit path), `quiet_buzz.txt` (no permission buzz while quiet, buzz again after) |
| 6 | IMU raise-to-wake and lift-during-glow (needs the BMI270 driver) | on-device only |

The simulator is the acceptance tool at each phase: scripts under `simulator/scripts/`, screenshots compared by eye, `expect_screen` extended with the mode name.

## 8a. Architecture: what is swappable

Three layers, so the look and the interaction model can change independently (user request, Phase 2):

| Layer | Lives in | Knows about | Does not know about |
|---|---|---|---|
| **Interaction model** | `nav.h`, `nav.c` | modes (`MODE_HOME`, `MODE_AGENT`, `MODE_ASK`, `MODE_STATUS`, `MODE_WATCH`), the gesture map (`nav_bindings[]`: mode × gesture × zone × condition → action; conditions `WHEN_REQUEST` = an approvable permission, `WHEN_ASKABLE` = any permission or question), the safety rules (`NAV_HOLD_MS`, `NAV_DENY_HOLD_MS`, `NAV_CARD_ARM_MS`, `NAV_LONG_SWIPE_PX`, `nav_hold_may_start()`, `nav_long_swipe_down()`), the per-state action lists (`nav_agent_items()`, minus Scroll once a session is `no_scroll`), the answer options with their commit rules (`nav_ask_items()`: which option is a hold, and how long) the scroll rate (`nav_scroll_lines()`, `NAV_SCROLL_*`: lines per step, coalescing window, clamps, timeout), and the watch's own rows with the quiet cycle (`nav_status_items()`, `nav_quiet_step()` / `nav_quiet_next()`, `NAV_QUIET_STEP*_S`, `NAV_STATUS_TIMEOUT_MS`) | LVGL, colours, geometry, labels |
| **Mode plumbing** | `ui.c` | which screen owns the current mode, mode transitions (`NAV_OPEN_AGENT`, `NAV_OPEN_ASK`, `NAV_OPEN_STATUS`, `NAV_BACK`, `NAV_HOME`, timeouts; a permission or question opens ASK on its session, done/error land on HOME), the debug report | drawing |
| **Presentation** | `ui_theme.h/.c` (tokens: palette, fonts, pill tint, key caps, badge angles), `ui_ring.c` (index ring), `ui_wheel.c` (the list component), `ui_home.c` (deck card), `ui_agent.c` (wheel screen: labels for action ids, header, badges), `ui_ask.c` (request text, option labels and colours, answer line; runs the hold the model allows), `ui_scroll.c` (counter, chevrons, privacy-gated preview line, the 50 ms coalescer and the predicted position), `ui_status.c` (link and battery header with the capsule gauge, row labels, the A badge's word per row) | how things look and where they sit | which gesture triggered an action |

What a change touches:
- **Reskin** (Capsules → something else): `ui_theme.h` tokens and the draw code in `ui_ring.c` / `ui_wheel.c` / `ui_home.c` / `ui_agent.c`. No gesture logic, no safety rule, no test script changes.
- **Gesture remap** (say, swipe left opens the wheel): one row in `nav_bindings[]`, plus its description in `atlas/tools/extract.py`. No drawing code.
- **New mode** (ASK, STATUS, WATCH): a `MODE_*` value, its rows in the table, a screen file with `open/close/act/tick/report`, and one case each in `enter_mode()`, `ui_tick()`, `ui_report()` and `ui_on_gesture()` in `ui.c`. SCROLL followed this recipe exactly (Phase 4); it also added one gesture kind (`G_DRAG`, with `dx/dy/flick` on every event) in `gesture.c`, which no other mode binds. STATUS followed it too (Phase 5) with no new gesture kind: ten rows in the table, `ui_status.c`, and the four cases in `ui.c`. Quiet's effect on alerts is one `model_quiet_active()` check in `app.c` (`on_attention`, `act_nag`), not in any screen.
- **New action for a state**: `nav_agent_items()` plus a label in `ui_agent.c` and a handler in `commit()`.
- **Draw rule** (any screen): refresh paths run on every session delta from any agent, and LVGL invalidates an object on every style write even when the value is unchanged. Use the change-only setters in `ui_theme.h` (`theme_set_text_color`, `theme_set_bg_color`, `theme_set_opa`, `theme_align`) and `theme_set_text` (cached, safe for text longer than the cache) for labels; a stray `lv_obj_set_style_opa` on the deck's full-screen centre once repainted all 466×466 px per delta. The deck writes the ring only in HOME, WATCH and STATUS (AGENT, ASK and SCROLL own it). Hidden screens are not refreshed: the deck card updates only in HOME (entering HOME refreshes it). Per-step and UI code reads the wall clock through `theme_now_unix()`/`app_now_unix()`, not `hal_rtc_get_unix()`: the RTC is an I2C read, so the core re-reads it every 10 s and counts seconds in between. `sim_perf_four_agents` and `sim_perf_core_screens`/`sim_perf_core_overlays` hold the budgets.

Safety is in the interaction layer, so every skin keeps it for free: a tap is never bound to approve (`{MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK}` only opens the options; on ASK `NAV_COMMIT` refuses any item with `hold_ms`), the deck has no `G_PRESS` binding at all, holds start only through `nav_hold_may_start()` (request on screen ≥ 300 ms, bound to its `rid`; `ui_ask.c` cancels the hold when the request changes, clears or is answered, or the selection moves), which options are holds and for how long is decided by `nav_ask_items()` (grant bit or Approve → 1.2 s, a permission's deny-class option → 0.8 s), button A on ASK is `NAV_FOCUS` (Open on PC), deny and back use the same `nav_long_swipe_down()` rule, the first touch after a glow is swallowed in `power.c`, and approve/deny/reject haptics are distinct patterns in `feedback.c`. The Atlas extractor reads the gesture map from `nav.c` and fails on a `MODE_HOME` press binding or any `MODE_HOME` deny binding, so a remap is documented automatically and a reskin cannot reintroduce a deck hold or a deck deny.

## 9. Decided (user answers, 2026-11-07)

1. **Tap centre opens the per-agent screen.** It is the wheel with gesture shortcuts layered on it (§4.2 explains why not a bare gesture pad). Swipe up does the same. Button A stays the eyes-free default action.
2. **Deny: both** the 0.8 s hold item and the strict swipe (≥ 120 px, starting above the centre).
3. **Scroll by vertical swipe/drag**, not by rim scrub. The rim is unused in SCROLL; session switching is horizontal or on the rim, so the two never meet (§3).
4. **Scroll entry only from the AGENT wheel.** Long-press centre on HOME is reserved (no action).
5. **"Don't ask again" grants are offered**, as hold items, gated by `attention.grant_mask`.
6. **Cancel agent: deferred** (no decision).
7. **A+B chord = Home.** Quiet mode lives only in STATUS (swipe down from HOME, or "Status…").
8. **Scroll preview: yes**, one line ≤ 48 chars, only when privacy ≥ preview.
9. **Pebble-style `[A] WORD` key badges.**

Phase 3 decisions (2026-11-08, implementation):

10. **`grant_mask` is an optional key** (`attention` key 8), sent whenever `opts` is: an older daemon without it still works (no grants offered), and the existing vectors stay valid. `cmd.arg` becomes `uint / tstr` in the CDDL (zcbor encodes `any` as `nil`); the watch sends Snooze with `arg = 30` explicitly.
11. **The deck no longer holds.** Approving is: open ASK (tap, swipe up, or automatically on arrival), then hold. One hold path to audit instead of two; §3's "HOME hold = —" now matches the code.
12. **Claude Code hooks: `PermissionRequest` + `PreToolUse`(`AskUserQuestion`), 15 s window, silence = no decision** (DESIGN §6a fallback 2). Multi-question / multi-select prompts and Copilot are not offered to the watch yet (multi-question and multi-select: proto 0.18, [LAST-MESSAGE.md](LAST-MESSAGE.md)). (2026-11-12: the window is a daemon setting, 60 s by default, shown on the watch as a countdown ring: [ANSWER-WINDOW.md](ANSWER-WINDOW.md). Silence still never decides.)
13. **Question defaults**: the first option is preselected (the agent's own default); ASK has no idle timeout, it waits for the answer or the daemon's `clear`.

Phase 4 decisions (2026-11-08, implementation):

14. **Scroll leaves by tap, A, B long, chord or timeout; swipe down is a page.** §3's SCROLL column already said so; the long-swipe-down back gesture of AGENT/ASK does not exist in SCROLL, so the centre's vertical motion keeps one meaning per mode.
15. **A page (B) supersedes the steps still waiting**, never a double move (§4.4). The device drops lines still waiting in its 40 ms window when B pages; the daemon's coalescer likewise lets `to_end` replace a queued burst. (Before 2026-11-09 a flick was a page; it is now a glide of lines.)
16. **kitty's position is tracked by the daemon**, corrected from `ls` when kitty reports `scrolled_by` and exact (with the top line) when the privacy setting lets the daemon read the text. `get-text --extent screen` would not give the scrolled view.
17. **No `welcome.feats` yet.** The device advertises `feature_scroll`; a daemon without scroll answers `unsupported` and the item disappears for that session. Adding daemon features to `welcome` is a protocol change for a later phase.
18. **Scroll is never audited** (read-only on the PC) and the preview never reaches the trace or the audit log; one trace line per coalesced batch.

Phase 5 decisions (2026-11-08, implementation):

19. **Quiet is device-side and timed.** The Quiet row cycles `settings.quiet_until` off → 30 min → 1 h → off; while it runs the device holds the attention buzz, the glow and the nag, and nothing else. The daemon is not told (`cmd_op_quiet` answers `unsupported` today); a daemon `cfg` still overrides `quiet_until` and the alert mode. Telling the daemon is a later phase, with `welcome.feats`.
20. **Sounds toggles the alert mode full ↔ quiet** on the watch; silent (no haptics) is only ever set from the PC, so a thumb cannot mute the watch by accident.
21. **On STATUS a tap commits, like A; swipe up goes back.** §3's earlier "tap = back" cell was replaced: lists keep one thumb motion everywhere (§1), and nothing on STATUS is destructive. Swipe down is ignored there, the rim tap too; the index ring keeps showing the sessions.
22. **Deny no longer exists on the deck.** `nav.c` still had `{MODE_HOME, G_SWIPE_DOWN, …, WHEN_REQUEST, NAV_DENY}`; it is now `NAV_OPEN_STATUS` unconditionally, as §3 said, and the extractor fails on any deck deny binding. The simulator's deny tests already ran in ASK.
23. **The empty deck is not STATUS.** It keeps `No sessions` / `Waiting for an agent` plus the hint `swipe down for status`; embedding STATUS would loop its 8 s timeout against an always-empty deck.
24. **No toast on STATUS, and no "Sleep now" row.** The row's new text and the haptic are the confirmation; the display-off path stays with `power.c`'s idle timer (§4.5 lists no sleep row).
25. **"Watch" opens the WATCH monitor** (§4.7); the STATUS screen is reached by the "Status…" item (and swipe down from HOME). `actions_wheel.txt` and `status.txt` check both.
26. **Toasts on the wheel screens use the hint line** (ASK, STATUS; `ui.c place_toast()`): the deck's slot over the action pill lands on the selected row of a wheel laid out in the bottom half. A toast that outlives a mode change moves to the new mode's slot.

Pared down (2026-11-09, user decision, issue 71da):

27. **The AGENT wheel holds at most three items, and SCROLL is gone.** WATCH made "Scroll terminal" redundant, so the SCROLL mode left the firmware (the daemon keeps its scroll support). "Status…" left the wheel (swipe down on HOME) and "Focus terminal" too (button A on HOME and WATCH), except on a session that waits for input. This supersedes decisions 3, 4, 8 and 14–18 on the device side, and the "Status…" item of decisions 7 and 25. The next input work is quick replies and interrupt (issue b150), then voice (issue 0ea9).

Quick replies and interrupt (2026-11-09, user decision, issue b150):

28. **The watch's first text input is a fixed list.** The daemon owns the replies (`~/.agentlink/replies.txt`, default Continue · Yes · Run the tests) and sends them in the welcome; the device answers with an index, so it can never inject arbitrary text (DESIGN §9). Interrupt is Esc after a hold. A working session's wheel has four items because of it; the three-item rule of decision 27 applies to everything else. Voice (issue 0ea9) is next.

Scroll from WATCH (2026-11-10, user decision, issue 5cbb):

29. **Scrolling is always one drag away from a running agent.** After decision 27 removed SCROLL, the user asked for it back ("the user should always be able to scroll"). The Phase 4 SCROLL mode returned unchanged, entered by a vertical drag in the centre of WATCH instead of a wheel item, and leaving it returns to WATCH. WATCH's swipe up no longer goes back (it is the start of a drag); B held or A+B does. The wheel stays as decision 27 left it.

Replies that fit the question (2026-11-10, user decision):

30. **Reply answers the agent's actual question.** The fixed defaults (Continue, Yes, Run the tests) "don't correspond to anything specific", so they are gone. The daemon reads the closing question from the turn's last message and offers its answers (ask.rs, no model or network); the watch shows the question above them. The user's own replies.txt entries follow.

Voice dictation (2026-11-10, user decisions, issue 0ea9):

31. **Dictation is local and confirmed.** whisper.cpp with base.en runs on the PC (no cloud), the watch streams IMA ADPCM, and the transcript is typed only after a hold, never automatically. It starts from "Dictate…" in the Reply list; a tap stops the recording.

Wheel cleanup (2026-11-10, user decision, issue 6696):

32. **No "Mark seen" row.** It was pointless: the user has already looked at a session by the time its wheel is open. Opening AGENT on a Done, unseen session sends `ack` once (the segment dims and leaves the attention cycle); button A on a Done session still acknowledges too. The Done wheel was *Reply…* · Watch (Watch now leads every wheel, decision 34). Dismiss error stays, since it also moves the session to idle.

Swipe keyboard (2026-11-10, user decisions, issue 1e57):

33. **KEYS: swipe on the watch, decode on the PC.** The watch only records strokes over a QWERTY layout; the daemon decodes them against a lexicon with the terminal's context and types the best word straight into the session's terminal, so the watch never shows the composed text. Candidates sit in the top cap; Enter needs a hold.

34. **Watch leads every wheel; holding still leaves WATCH** (issue dd44, 2026-11-10). The user asked for "tap, tap" from the deck to the monitor, and a third tap focuses the terminal, so Watch is the first (default) item in every state. WATCH also needed a touch way back besides B long and the A+B chord: a long press on the centre (600 ms without moving) is unused everywhere else and cannot be mistaken for the drag that scrolls, which starts only once the finger moves.

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
| Terminal scroll | — | — | **new**: SCROLL mode by trackpad drag and glide, `cmd_op_scroll`, kitty + tmux |
| IMU | lift/tilt wake | deferred | raise-to-wake only, once a driver exists; **never a commit input** |
