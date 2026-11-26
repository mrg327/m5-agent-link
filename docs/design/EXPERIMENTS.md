---
status: implemented
owner: ui
issue: 20261113-61ce
last_reviewed: 2026-11-26
summary: LABS, the Trackpad, the Personality switches, the toys' groundwork and the decision toys
---

# Experiments (LABS), the Trackpad and Personality

Issue 61ce. Experiments are features that sit outside the agent flow. They may change or go away. The toys'
groundwork (issue 20261126-b5d1) and the first toy, Decide (issue 20261126-74ee), are in [Toys](#toys).

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

A toy is something to play with for a few seconds (a coin, a die, a timer), listed in LABS after the Trackpad
and Personality. One ships, [Decide](#decide-coin-die-and-oracle). This is the groundwork every toy uses, so a toy
is one `ui_<toy>.c` plus a pure `<toy>.c` and a row in `s_builtin` (`ui_toy.c`). The host is one mode, `MODE_TOY` (`ui_toy.h`): it gives the toy a root to draw
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
again. The timed Quiet of STATUS is for agents, so it does not hold toys back. The fun word's toys bit
(`FUN_TOYS`, asked through `persona_allow(PF_TOYS, ...)`) off keeps every toy still (`toy_in()` in `feedback.c`)
and takes the toys out of LABS (`ui_toy_allow()`); reduced motion (`FUN_REDUCED`) shows a toy's end frames.

**Timers obey the alert mode, not timed Quiet.** A timer the user set (a later toy) buzzes and sounds by the alert
mode as an alert does (full: buzz and cue; quiet: buzz; silent: neither). The timed Quiet does not silence it,
and face down nothing sounds.

**Sound memory.** Toys render into one static block, `sound_toy_pcm()` (`SOUND_TOY_SAMPLES`, 200 KB, in PSRAM on
the watch), with `sound_toy_render()`; a runtime note is a `snd_sound_t` on the stack. It is apart from the six
alert-cue slots of `sound_play()`, which a toy never touches. Nothing is allocated.

**Random numbers and motion.** `toy_rand.c` is xorshift32 stirred with the microsecond of each key or touch edge
and the tilt sample of a flick; `hal.h` has no hardware random source, so this needs no HAL change. A script's
`toy_seed N` restarts it and stops the stirring, so simulator runs repeat. `toy_motion.c` finds a flick (the
gravity vector moved `TOY_JOLT_MG` between samples of `motion_pose()`, one per `TOY_JOLT_GAP_MS`) and a still hold
(face up and still after a move, so a watch left on the desk never holds by itself) from the 10 Hz tilt samples:
no board emits `HAL_MOTION_SHAKE`. Both read the size of a change and z only, so they hold whatever the board's
x/y signs turn out to be.

### Decide: coin, die and oracle

Issue 20261126-74ee (the UX research's idea 17): for "tabs or spaces, who reviews, which task first", five
seconds and back to work. One toy, `ui_decide.c` over `decide.c`, with the dice glyph in LABS. B (on release)
steps coin, die, oracle; three pips at the legend row say which. B held goes back to LABS, the chord Home.

| | Look | Throw | Lands with |
|---|---|---|---|
| Coin | a white disc r 90, blank until the first throw | A (at once, raw keys), a tap, or a flick | five or six half-turns about its vertical axis in 1 s, slowing (a detent each), on a black tick or cross; a 60 ms pulse and a two-note cue |
| Die | a ring r 156 in as many pieces as sides, filled to the number; the number 56 px | as the coin; the bezel picks 4, 6, 8, 12 or 20 sides (a detent a step) | nine numbers at 60 to 240 ms (a detent each), then the result; a 50 ms pulse and its cue |
| Oracle | the hand-tap glyph in a dim ring | held 2 s: a finger in the middle, key A, or the watch face up and still | the grey ring fills; one of six glyphs rises 20 px into place; a 70 ms pulse and a bell |

The oracle's six answers are 128 px glyphs, all white (a green tick would read as "done"): tick (yes), cross
(no), hourglass (later), the working arrows (ask again), moon (sleep on it), speech bubble (unclear). They are to
be tried on people before they are settled. The result is drawn when the throw starts; the animation only shows
it arriving. Decide sets `not_over_request`: LABS will not open it while a request waits, and if the watch
comes back to it with one still waiting (B held on ASK), a throw or a hold is refused with the hand sign and
the reject buzz. A request that arrives mid-toss freezes the throw; the toy comes back dim, and a tap or A
carries the throw on to its landing. A hold ends with the takeover.

## Personality (issue 20261126-98a6)

The second item (the waveform glyph) is the fun switchboard, until STATUS has rows. It needs no link.

- An orbit of toggle discs, eight to a page with page pips, one glyph per switch: Living marks (waveform),
  celebrations (check), motif (speaker), the Mote (dot), hello and close (clock), replay (fast-forward), easter
  eggs (asterisk), trail (calendar), break cup (timer), toys (hand tap), reduced motion (stop). An off switch
  is a dim disc; the selected switch's glyph is the subject, white when on and grey when off.
- A, or a tap on the selected disc, flips it; a tap on another disc selects it; B and the dial move. A swipe
  down or right, B long, or a hold off the discs goes back to the experiments; the chord goes home.
- Each flip writes the stored `fun` word (`prefs_set_fun`, NVS key `fun`) at once. What each bit does, its
  default and the rules every feature obeys are in [GLYPHS.md](GLYPHS.md) §3.11 and §3.12. Living marks,
  replay (§3.13), the Mote (§3.14: a light on the idle face, off by default) and the easter eggs (§3.15, on)
  exist; the other switches are there for the features filed beside them (celebrations, toys …), which ask
  `persona_allow()` for their bit.
- Arrivals wait here as on the rest of LABS. No word is drawn; the report names each switch with "on" or
  "off".

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
- `simulator/scripts/labs_fun.txt` covers Personality: entry, flips, selection, the back paths, and the deck's
  mark holding still once Living marks is off. The gate and the poses are `test_persona.c` and
  `test_temper.c`; the motion is `fun_temper.txt` and `fun_temper_states.txt`.
- Protocol vectors: `cmd_pointer_*.json` and `welcome_pointer.json`.
