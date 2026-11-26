---
status: implemented
owner: ui
issue: 20261113-61ce
last_reviewed: 2026-11-26
summary: LABS, the Trackpad and the toys' groundwork
---

# Experiments (LABS) and the Trackpad

Issue 61ce. Experiments are features that sit outside the agent flow. They may change or go away. The toys'
groundwork (issue 20261126-b5d1) is in [Toys](#toys).

## Entry

- A swipe up on HOME (a centre start) opens **LABS**, a wheel titled "Experiments". The swipe used to open
  AGENT; tap centre still does that (UI-V2 decision 35).
- In LABS, scrub or B selects; tap, double tap or A opens the selected item.
- To go back to HOME: a long swipe down or a swipe right, B long, the A+B chord, or 8 s idle
  (`NAV_LABS_TIMEOUT_MS`).
- An item that cannot run is still listed. Its description turns amber, the A badge hides, and opening it shows a
  toast and the reject haptic. Trackpad is blocked while offline, or when the daemon's welcome has no `pointer`
  (an older daemon or no backend).

## Trackpad

The watch drives the pointer of the machine `agentlinkd` runs on (protocol §6g, `cmd_op_pointer` = 20).

| Input | Result |
|---|---|
| Drag starting in the centre | moves the pointer |
| Tap | left click; a second tap within 400 ms and 40 px of the last one is sent with `n` = 2 (a double click), up to 3 |
| Hold still 600 ms | right click |
| Turn on the bezel (scrub) | scrolls: clockwise = down, one notch per 15° (24 sectors, wrapped at 24 so crossing 12 o'clock is one notch), with a detent haptic |
| A, B long | back to LABS |
| A+B chord | HOME |

- **Moves.** The gesture recognizer reports `G_MOVE` per touch sample once a centre-start press leaves the 18 px
  tap slop, and only while the trackpad has turned moves on (`gesture_set_moves`). Each step gets a gain of
  1.2 + 0.08 per px of the step, capped at 4.0 (`nav_pointer_gain10`), so slow drags are precise and flicks
  travel. Tenths are carried between steps. Moves are coalesced and sent every 30 ms
  (`NAV_POINTER_COALESCE_MS`).
- **Clicks.** The recognizer has no double tap here: a tap fires on release, so the watch counts double clicks
  itself and every click is sent at once.
- **Swipes** are ignored on the trackpad, so a fast drag never leaves it.
- The link is held interactive while the trackpad is open (`hal_link_interactive`).
- **Failures.** The daemon answers a failure at most once a second. The hint shows its message (on macOS, "Allow
  agentlinkd in Privacy & Security > Accessibility"), with a toast and the reject haptic.
- **Arrivals.** LABS and the trackpad keep their work (`ui.c` `mode_keeps_work()`): an arriving request does
  not take the screen. It buzzes and sounds as usual, then waits as a dot on the rim, and opens when you are back
  on the deck or WATCH, as on KEYS, VOICE, READ, GRAPH and STATUS. This is deliberate: the pointer is in the
  middle of work on the PC. Toys do the opposite ([Toys](#toys)).

## Toys

A toy is something to play with for a few seconds (a coin, a die, a timer), listed in LABS after the Trackpad.
None ships yet; this is the groundwork every toy uses, so a toy is one `ui_<toy>.c` plus a pure `<toy>.c` and a
row in `s_builtin` (`ui_toy.c`). The host is one mode, `MODE_TOY` (`ui_toy.h`): it gives the toy a root to draw
on, every gesture with its nav action, and calls `open`, `pause`, `tick` and `report`.

**An agent always comes first.**

1. **The alert is untouched.** Arrival, nag, finish and usage-warning feedback run as before. Each first stops a
   toy's microphone (the speaker is silent while it records, so the cue would be lost), then its sound and buzz,
   and plays its own.
2. **A request or an error takes the screen at once.** `MODE_TOY` is not in `mode_keeps_work()`, so ASK (or the
   error card) opens. The host calls the toy's `pause`, turns raw keys off and stops what it plays. A toy keeps
   its state in statics, so nothing is lost. A key or touch that began before the takeover, or within
   `TAKEOVER_SETTLE_MS` (400 ms) after it, does not act on ASK; a key held across the switch makes nothing more.
3. **Back to the toy.** When ASK closes (answered, cleared on the PC, or B held to go back), the watch returns to
   the toy, **paused** (`s_resume_toy` in `ui.c`, the twin of `s_resume_watch_sid`). A tap or A resumes it. If
   another request waits, it takes over from there at once. Leaving ASK another way (the chord) goes Home; the
   toy stays paused and LABS opens on it next time. After an error card the toy stays paused the same way.
4. **A finish only quiets the toy.** A done does not take the screen. For the length of its cue and buzz (at
   least `TOY_ALERT_HOLD_MS`, 1.5 s) the toy gate refuses toy sound, buzz and microphone.
5. **A toy that could sway an answer** (a die, an oracle) sets `not_over_request`: LABS will not open it while a
   request waits (the hand sign and the reject buzz).

**Raw keys.** A toy that sets `raw_keys` gets each key edge at once: `G_KEY_DOWN` / `G_KEY_UP` (x = 0 for A, 1
for B, `dur_ms` on the up) as `NAV_TOY_KEY`, with no 300 ms double-press wait (`gesture_set_raw_keys`). B held
(`G_BTN_B_LONG`) still goes back to LABS and A+B still goes Home, so a toy never uses both keys together. Raw
keys are off on every other screen. Without them, A is `NAV_COMMIT` and B `NAV_NEXT`. A tap is `NAV_COMMIT`, the
rim `NAV_SCRUB`, and a swipe down or right goes back.

**Feedback through the gate** (`toy_gate.c`, `feedback.c`). Toys never call the alert feedback; they use
`g_ui_actions->toy_buzz`, `toy_detent`, `toy_play` and `toy_mic`, which return false when refused:

| | Full | Quiet | Silent | Face down | Volume 0 | Mic on | Alert hold-off |
|---|---|---|---|---|---|---|---|
| Buzz, detent | yes | yes | no | no | yes | yes | no |
| Sound | yes | no | no | no | no | no | no |

A toy's sound plays at its own level, never above the stored one; the next alert cue sets the stored level
again. The timed Quiet of STATUS is for agents, so it does not hold toys back. A placeholder in `feedback.c`
(`toy_in()`) is where the coming fun preference will keep every toy still.

**Timers obey the alert mode, not timed Quiet.** A timer the user set (a later toy) buzzes and sounds by the alert
mode as an alert does (full: buzz and cue; quiet: buzz; silent: neither). The timed Quiet does not silence it,
and face down nothing sounds.

**Sound memory.** Toys render into one static block, `sound_toy_pcm()` (`SOUND_TOY_SAMPLES`, 200 KB, in PSRAM on
the watch), with `sound_toy_render()`; a runtime note is a `snd_sound_t` on the stack. It is apart from the six
alert-cue slots of `sound_play()`, which a toy never touches. Nothing is allocated.

## Daemon

`daemon/crates/agentlinkd/src/pointer.rs` has three backends:

- **macOS:** CoreGraphics events. Needs the Accessibility permission. With an ad hoc signature it is lost on
  every rebuild; `launchd.sh cert` (once) makes the signature stable so the grant survives (daemon/README.md).
- **Windows:** `SendInput`.
- **Linux on X11:** `xdotool`. There is no backend under Wayland.

`welcome.pointer` (key 7) is set only when a backend exists.

## Not yet

- Drag (click and hold to drag) is deferred.
- There is no horizontal scroll.
- The pointer is always that of the daemon's own machine, not of a remote session's host.

## Tests

- `simulator/scripts/labs_trackpad.txt` covers entry, the blocked item, clicks, double click, right click, a
  move, a scroll and the back paths.
- `simulator/tests/test_gesture.c` covers `G_MOVE` and raw keys.
- `simulator/tests/test_toy_gate.c` covers the gate; `test_feedback.c` covers an alert stopping a toy's sound
  and microphone, and the hold-off.
- `simulator/scripts/toy_yield.txt` drives the simulator's stub toy (script command `toy_stub`): raw keys, the
  refusal while a finish's cue plays, a request taking over and the return to the paused toy.
- Protocol vectors: `cmd_pointer_*.json` and `welcome_pointer.json`.
