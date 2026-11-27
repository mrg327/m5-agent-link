---
status: implemented
owner: tools
issue: [20261122-bb2e, 20261126-59e0]
last_reviewed: 2026-11-26
summary: Drive the real watch like a finger and measure what the user feels
---

# Bench: drive the real watch like a finger, measure what the user feels

Issue 20261122-bb2e. Protocol 0.24 (§2). Code: `components/core/src/bench.c` (stroke player and frame timeline),
`daemon/crates/agentlinkd/src/bench.rs` (bench mode on a link), `daemon/crates/agentlink/src/bench.rs`
(`agentlink bench`), `bench/tasks/*.txt` (the task scripts).

The simulator replays touch input through the real gesture recognizer, but at the desktop's speed. The watch has
the real speed, but until now it could only be handed finished gestures, and it reported one frame-rate number a
second. The bench closes that gap. The daemon plays a script on the real watch as finger strokes. The watch records
every frame it draws, and every touch, gesture and command, on its own clock. The daemon then turns those records
into a report: how long each task took, how fast the screen answered a touch, and how smooth the animations were.

Decisions (2026-11-22): no camera; the first tasks are approve, answer, reply and switch; the result is a
report, not a pass/fail gate.

## 1. What is measured

| Measure | From | Meaning |
|---|---|---|
| Task time | first touch-down → the command the task ends with | how long the task takes a person, input and screen included |
| Inputs | strokes and button presses in the task | effort |
| Touch to frame | each touch-down or lift → the first frame drawn after it | how fast the screen visibly answers |
| Frame time | start of rendering → end of the panel transfer, per frame | p50, p95 and max per task |
| Frame gaps | time between consecutive frames inside an animation | slow frames: gaps over 33 ms (one LVGL period) and over 50 ms |
| Panel wait | time LVGL spent waiting for the panel transfer | whether a slow frame is drawing or transfer |
| Pixels | pixels flushed per frame | how much each frame redrew |

All times come from the watch's own clock (`hal_time_us`), so BLE latency does not enter any measure except the
start of a stroke, which the watch timestamps when it plays it.

Limits: the timeline shows when a frame reached the panel controller, not when the AMOLED lit. With no camera,
the glass itself is out of scope.

## 2. Protocol 0.24

Three debug messages (types 64 and up are debug, PROTOCOL.md §0). The daemon sends them only to a watch with proto
0.24 or later that offers `feature_debug`.

```text
mt_dbg_touch  = 70   ; daemon → device: one finger stroke to play
mt_dbg_bench  = 71   ; daemon → device: start, stop or mark the frame timeline
mt_dbg_frames = 72   ; device → daemon: timeline records

dbg_touch = {
  0 => uint,     ; dt    ms between samples, 1..2000; the finger lifts dt after the last sample
  1 => bstr,     ; pts   int16 little-endian x, y pairs, 1..512, in the coordinates the user sees
}
dbg_bench = {
  0 => uint,     ; op    0 start (clear, record), 1 stop (send the records), 2 mark
  ? 1 => uint    ; arg   op 2: a number the daemon writes into the timeline (a script step)
}
dbg_frames = {
  0 => uint,     ; seq   index of the first record in this message
  1 => bstr,     ; recs  16-byte records (§3), a whole number of them
  2 => bool,     ; last
  3 => uint      ; lost  records not kept because the buffer was full
}
```

A stroke presses the finger at the first point when it arrives, moves it to each next point every `dt` ms, and
lifts it `dt` ms after the last. A tap is one point with `dt` 60, a hold is one point with `dt` equal to the hold,
and a swipe is a point every 10 ms (the touch read period). The player sits in the touch read callback, under
LVGL and the gesture recognizer, so they see a finger. The panel's real touches are ignored while a stroke plays. The touch is polled (every 10 ms when the loop is free), and a read can come tens of ms late while a frame renders, so the stroke's clock starts at the first read that plays it: the watch sees the whole stroke, a short tap included. The down record keeps how late that read was.
A stroke that arrives while another plays lifts the first one. A stroke reaches the UI as a finger does: on a dark
display its press only wakes it, and on a glow it only reveals the request.

The watch logs each stroke, so a run whose strokes are ignored can be read without a debug build (issue 435a):
`bench touch` adds `read_ago` (ms since the last touch read, -1 before the first), `prev` / `prev_seen` (a stroke
was still playing, and whether a read had played it) and `down` (what that read reported). `bench stroke_end` gives
the reads that played it, `panel` (those where the real panel was also pressed: a finger or a stuck panel) and
`late`. `bench touch_unread` (warn) means no read played a stroke within 500 ms, with the `read_ago` at that point.

A debug link can already inject gestures (`dbg_inject_input`), so a stroke grants nothing new. It is accepted
only from a welcomed daemon, like every other debug message.

## 3. The timeline

`bench_start` wakes the display, as a person raising the watch would, and allocates 4096 records (64 KB, PSRAM on the watch) once and clears them. Recording stops when the
buffer is full: the start of a run is kept and `lost` counts the rest. At 30 frames a second that is over two
minutes of animation.

Each record is 16 bytes, little-endian:

| Offset | Field | |
|---|---|---|
| 0 | `t` u32 | µs since `bench_start` |
| 4 | `a` u32 | frame: µs from render start to the end of the flush; down: µs from a stroke's arrival to the read that played it (0 for a real finger); other kinds: 0 |
| 8 | `b` u32 | frame: µs spent waiting for the panel; other kinds: the argument below |
| 12 | `c` u24 + `kind` u8 | frame: pixels flushed; touch: x << 12 \| y |

| Kind | | `b` |
|---|---|---|
| 0 frame | one LVGL refresh that drew something (pixels > 0) | panel wait µs |
| 1 down | the finger touched (stroke or real) | — (`c` holds x, y) |
| 2 up | the finger lifted | — |
| 3 gesture | the recognizer's output, or a button | `gesture_kind_t` |
| 4 mark | a `dbg_bench` mark from the daemon | its `arg` |
| 5 cmd | the watch sent a command | `cmd_op` |

`bench_stop` stops recording and streams the records in `dbg_frames` messages of 64 records (1 KB) from
`core_step`, a few per step, like the screenshot.

## 4. Bench mode in the daemon

A task needs a known screen: a session in a given state, an approval card. The daemon builds that screen on the
real watch, so a test approval must never reach a real agent. While a bench runs on a link:

- updates from the registry and the broker are not sent to that watch; the session list it shows is the
  script's;
- every command the watch sends goes to the bench, which records it and answers `result ok`, as the simulator's
  scripts do; nothing reaches an agent or a terminal;
- when the bench ends (or the CLI disconnects), the link sends a fresh snapshot, the pending requests and the
  usage, as after a lagged update.

`agentlink bench run` refuses to start while a real request is waiting for an answer. A request that arrives
during a run waits on the watch for its answer window as usual, so it may fall back to the terminal; a run takes
under a minute per task.

## 5. Scripts and the user model

Task scripts use the simulator's script language (simulator/README.md), so the same file runs in `ctest` and on
the watch. The daemon's player understands this subset:

```text
wait MS | tap X Y | hold X Y MS | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long|double] | lift
session SID STATE REPO [BRANCH [SUMMARY]] | attention RID SID approval|question ACTS GRANT TITLE|TEXT|OPT...
bench mark N | expect_cmd OP | expect_screen NAME | question SID Q|ANSWER... | scrub DEG0 DEG1 [MS [RADIUS]]
```

`expect_cmd OP` waits (up to 10 s) for the watch to send that command and ends the task's clock there; the
simulator checks the same thing against the frames the device sends. `expect_screen` asks the watch for its
`dbg_ui_state`.

`--human SEED` turns the script's exact input into a person's: every stroke lands at a point drawn around the
target (σ 6 px, cut at 3σ), and every `wait` before an input gets a reaction time added (log-normal, median
250 ms). The same seed gives the same run. Without it the run is exact, which is what comparing two builds needs.

## 6. The report

`agentlink bench run TASK... [--runs N] [--human SEED] [--out DIR]` writes `report.md`, `report.json` and one
`timeline-<task>-<run>.csv` per run to `~/.agentlink/bench/<time>/` and prints the report. Per task, over the runs:
the median and worst task time, the inputs, the touch-to-frame p50 and p95, the frame time p50, p95 and max, the
slow-frame count and the share of frame time spent waiting for the panel. The header names the firmware and
daemon versions and the link (BLE interval or USB), because the numbers depend on all three.

In the simulator, `bench start` / `bench save FILE` record and write the same records, and
`agentlink bench report FILE` reads them. Those times are the desktop's, useful for counting frames and pixels,
not for speed.

## 7. Running it

The watch needs firmware with proto 0.24, and the daemon that runs the bench must hold the watch's link. A daemon
installed as a service can run it directly (`agentlink bench run bench/tasks/*.txt`). To try a daemon build
without touching the service, run that build on USB alone, with its own home; a plugged-in watch prefers USB, and
goes back to the service's BLE link when the bench daemon stops:

```sh
ln -sfn /some/dir /tmp/alb    # a short home: Unix socket paths are limited to about 100 bytes
AGENTLINK_HOME=/tmp/alb AGENTLINK_USB=1 AGENTLINK_BLE=0 AGENTLINK_WS_ADDR=127.0.0.1:7979 \
  AGENTLINK_PRESENCE=off AGENTLINK_KEEP_AWAKE=off daemon/target/release/agentlinkd &
AGENTLINK_HOME=/tmp/alb daemon/target/release/agentlink bench run bench/tasks/*.txt --runs 3 [--human 1]
```

The task scripts carry a proto 0.24 welcome for the simulator, so its menus match the watch's (the agent graph
item exists from 0.22 on).

First results (2026-11-22, StopWatch over USB, firmware cb1eabc plus the bench fixes, exact input):

| Task | Time ms | Touch read late ms | Touch→frame ms (p50 / p95) | Frame ms (p50 / p95 / max) | Anim fps |
|---|---|---|---|---|---|
| approve | 1159 | 52 | 289 / 294 | 7.3 / 63 / 79 | 29 |
| answer | 55 | 52 | 77 / 129 | 49 / 53 / 53 | – |
| reply | 2582 | 10 / 51 | 89 / 156 | 37 / 74 / 75 | 16 |
| switch | 950 | 11 | 23 / 31 | 9.1 / 15 / 58 | 30 |

What they say: on ASK the touch is read about 52 ms late, because the loop is busy drawing; a held disc shows
nothing for about 290 ms after the finger lands; the agent and reply screens draw at 16 fps, with median frames of
37 ms against the 33 ms period. With `--human 1` the task times grow by the reaction times (reply 3.3 s) and the
frame figures stay the same.

## 8. Later

- Filmstrips: keep the changed area of N frames after a trigger in PSRAM and upload them, so an animation can be
  checked frame by frame.
- Pace the simulator by the costs the watch measures, so a simulator animation has the watch's frame rate.
- Many seeded runs of the user model in the simulator, for miss rates on small targets.

## 9. Frame cost of the fun motion (simulator model, 2026-11-26)

Issue 20261126-59e0. The Mote (GLYPHS.md §3.14) and the fun motion beside it, checked against the frame budget
before the watch measures it: frames of 33 ms or less (`LV_DEF_REFR_PERIOD` 33, FIRMWARE.md §5), the cadence each
animation asks for, and no late first frame when an alert arrives. The numbers are the simulator's pixels and a
model of the watch's time; the bench tasks at the end measure the real ones.

**Per frame in the simulator.** `px_reset`, then `frames [all]` or `expect_frame_px_max PX [AREAS]`
(simulator/README.md), log every LVGL refresh that flushed pixels: the pixels, the areas after LVGL joined
them, the 48-row strips they take in the watch's buffers, the gap to the next frame and the host CPU of the
refresh. It only reads LVGL's display events, so the firmware draws exactly as before. The simulator's clock is
now the same in every run (virtual from before the core starts, reading one hour at start-up), so a script
draws the same frames each time; before, a clock-keyed animation started at a phase set by the host's uptime.

**From pixels to the watch's milliseconds.** Pixels alone do not predict the time: a full frame of mostly black
(217 k px) takes 70-110 ms on the watch (UX-PROFILE P1, about 0.35-0.5 µs a pixel), while six agents' comets
(8 k px of anti-aliased arcs) take 20 ms (about 2.5 µs a pixel). What the simulator's CPU spends on a frame
follows the content, so the model scales it: **watch ms ≈ K × host CPU ms, with K ≈ 50**, fitted on the bench
tasks of UX-PROFILE §2 run in the simulator (frame p50 on the watch over host CPU p50: watch 44, switch 47, labs
47, arrival's full frames 48, approve 49, busy 55, agent_back 59; STATUS's 107 ms frames give 86). A prediction
is therefore good to about -10 % / +70 %: the table gives K 50 and, in brackets, the worst case at K 85. Host CPU
is noisy under load; the figures are the lowest of four runs. The panel transfer is not the limit: QSPI at
80 MHz on 4 lines is 40 MB/s, 0.05 µs a pixel at line rate (about 0.07 with command and DMA gaps,
docs/research/ui-animation.md §3.1), so a 5 k px Mote box takes about 0.35 ms on the wire and a full frame
11-15 ms, overlapped with drawing by the two 466 × 48 buffers. The bench's panel-wait share checks this.

| Scenario (script) | Asks | Frames/s | px / frame p50 · p95 · max | Areas p95 · max | Watch ms p95 · max |
|---|---|---|---|---|---|
| Idle face: visit, lingering (`fun_mote`) | breath, 8 levels in 4 s | 3.9 | 4 225 · 4 225 · 4 225 | 1 · 1 | 8 · 8 (14) |
| Idle face: travel, half the ring | 30 fps for 4.4 s | 28 | 4 620 · 4 830 · 5 040 | 1 · 1 | 9 · 11 (19) |
| Idle face: big finish, drift | 30 fps | 30 | 4 620 · 4 896 · 7 539 | 1 · 2 | 9 · 12 (21) |
| Idle face: big finish, loop (600 ms) | 30 fps | 30 | 4 958 · 10 201 · 10 201 | 1 · 1 | 16 · 16 (27) |
| Idle face: big finish, flare (600 ms) | 30 fps | 19 | 5 041 · 9 801 · 9 801 | 1 · 1 | 14 · 14 (24) |
| Idle face: linger by the finish | breath | 9.8 | 4 355 · 4 554 · 4 690 | 1 · 1 | 8 · 9 (15) |
| Idle face: rest at 12, and night | breath | 3.5 | 4 225 · 4 225 · 4 225 | 1 · 1 | 9 · 9 (16) |
| Idle face: tired (6 s breath) | breath | 2.3 | 4 225 · 4 225 · 4 225 | 1 · 1 | 13 · 13 (22) |
| Idle face: ember | still | 0 | – | – | – |
| Idle face: hover at an error (`fun_mote_error`) | 30 fps sway | 26 | 4 290 · 4 355 · 4 355 | 1 · 1 | 8 · 8 (14) |
| **Boot: wake to size L** (`mote_boot`) | 30 fps | 24 | 15 129 · 15 625 · 15 625 | 1 · 1 | **27 · 27 (46)** |
| **Boot: flare at size L** | 30 fps | 30 | 23 409 · 38 025 · 38 025 | 1 · 1 | **56 · 56 (95)** |
| **Boot: rise to 12** | 30 fps | 27 | 10 791 · 38 809 · 38 809 | 1 · 1 | **57 · 57 (96)** |
| Empty deck: lit, breath and wander (`mote_empty`) | breath, 0.5°/s | 5.5 | 4 225 · 4 290 · 4 290 | 1 · 1 | 8 · 8 (14) |
| Empty deck: ambient | breath, 0.5°/s | 3.8 | 4 225 · 4 290 · 4 356 | 1 · 1 | 8 · 8 (14) |
| Empty deck: ember; pairing: waiting; update: waiting | still | 0 | – | – | – |
| **Pairing: flare** (`mote_pair`) | 30 fps | 27 | 5 929 · 20 851 · 20 851 | 2 · 2 | **28 · 28 (47)** |
| Update: arc ride, a step (`mote_ota`) | with the progress | – | 2 821 | 1 | 10 (17) |
| Update: flare before the restart | 30 fps | 5 | 625 · 729 · 729 | 1 · 1 | 7 (12) |
| **Replay with the Mote** (`mote_replay`) | 10 steps a second | 11 | 2 816 · 112 726 · 112 726 | 12 · 12 | **65 · 65 (111)** |
| Replay, the Mote off (`fun_replay`) | 10 steps a second | 11 | 2 112 · 112 726 · 112 726 | 12 · 12 | 60 · 60 (102) |
| Egg: clock slip (`fun_eggs`) | 3 frames of 90 ms | 2 | 20 026 · 20 026 · 20 026 | 1 · 1 | 8 (14) |
| Egg: century, the Mote's loop | 30 fps for 1.2 s | 14 | 4 422 · 4 556 · 4 556 | 1 · 1 | 8 (14) |
| Egg: century, the sigil's spin, during the ring's done sweep | 30 fps | 9 | 3 249 · 114 915 · 114 915 | 11 · 11 | 65 (110) |
| Living marks: breath and the comet (`fun_temper`) | 30 fps comet | 30 | 2 774 · 6 271 · 9 094 | 3 · 5 | 11 · 15 (25) |
| Living marks: the comet alone | 30 fps | 30 | 2 568 · 4 999 · 6 108 | 3 · 4 | 8 · 10 (17) |
| Living marks: done dip and joy, with the done card and ring (`fun_temper_states`) | 30 fps | 13 | 112 726 · 217 156 · 217 156 | 20 · 20 | 70 (119) |
| A request's ring pulse, attention screen (`fun_temper`) | 1 Hz pulse | 29 | 1 585 · 56 641 · 57 900 | 8 · 8 | 28 · 31 (53) |

Frames/s is over the window and counts only frames that drew; a breath asks for a level change, not a rate.

What it says:

- **The Firefly on the idle face fits.** Every mode is the orb's box, old and new, joined into one area of
  4-5 k px (about 10 k px at the loop and the flare), predicted at 8-16 ms a frame: the 30 fps trips, the
  hover and the loops keep their cadence, and the breath repaints 2-6 times a second. The empty deck, the update
  ride and the centuries loop are the same box. On battery, as an ember and while anything needs you, nothing
  is drawn.
- **Over the budget: the boot move.** At size L the halo is 12 rings of up to 96 px radius, and LVGL masks each
  round border over its whole box, so a frame of the flare or of the start of the rise (up to 39 k px) costs
  about as much as a full frame: predicted 27-57 ms (up to 96). The 1.2 s move then plays at about 18-25 fps
  from 0.3 to 0.9 s. It delays nothing else: the deck is up and takes input under it, and any input or request
  ends it in that step.
- **The pairing flare, fixed** (was about 21 k px, 28 ms predicted, up to 47, because the toast drew the
  "Paired" sign hidden under the passkey screen): the sign is now drawn only once the screen goes (owner,
  2026-11-27), so the flare is the orb's box alone, about 10 k px a frame (`mote_pair.txt`).
- **Beside the Mote, not of it** (issue 20261126-3385): the replay's step where a capsule changes (one 113 k px
  frame, inside its 100 ms step), the ring's done sweep (115 k px every other frame), the done card's change,
  the ambient face's 1 px burn-in move each minute (one full frame), and a request's ring pulse.
- **An alert's first frame.** The Mote leaves in the step the request lands (on the model's word, not the
  gate's step-old context), and its old box joins the request's own frame, so it adds no frame. What can delay
  that first frame is the frame already being drawn: up to about 12 ms behind the Firefly (21 at worst), 16 ms
  (27) at a finish's loop, 57 ms (96) during the boot move, 28 ms (47) during the pairing flare (pairing already
  counts as needing you), and, beside the Mote, up to about 110 ms when the request lands on the ambient face's
  minute move. `mote_alert.txt` measures it on the watch.
- **`sim_fun_temper`'s 4 M px window was test timing, not frame cost.** It is the request's own pulse (about
  56 k px a level, about 10 levels a second: 4.04 M px with Living marks off), and its sum moved with the phase
  the host's uptime gave the clock: 3.76-4.15 M px over runs, so a 4 M budget failed about one run in six, with
  or without load. With the clock the same in every run it is 4.10 M px; the budget is now 4.3 M, the worst
  phase seen plus 3 %. The same correction applies to `fun_mote.txt`'s loop-and-flare window: its "about
  0.4 M px" held the minute's full frame, and the Mote's own share is about 0.17 M.

**Tried and dropped.** Drawing each halo ring in row bands clipped to its own span (the same pixels, checked
on 46 shots) cut the blended pixels by a third but made each frame slower in the simulator, because each band
is a draw task with its own masks. The orb is drawn as before.

**Decided (owner, 2026-11-27):** the boot move stays as it is, accepting about 20 fps for 0.6 s at power-on
(option (a)); the pairing flare's sign moves after the flare. The options were: (a)
accept about 20 fps for 0.6 s at boot, the floor docs/research/ui-animation.md §9 sets for transitions; (b) draw
the halo from an alpha map computed once per look into PSRAM (about 38 KB at the flare) and blended once a
pixel instead of about five masked passes: the same falloff with different anti-aliasing, to compare on the
glass; (c) fewer, wider bands at size L (8 instead of 12); (d) a smaller boot flare (× 1.3) or the boot at
size M. For the pairing flare: show the "Paired" sign when the passkey screen goes, not under it (its entrance
is then seen after the flare instead of hidden).

**On the watch** (not run yet): `agentlink bench run bench/tasks/mote_*.txt --runs 3` over USB, with the Mote
switched on by hand (LABS > Personality, the dot; the simulator stores it with `--pref fun=0x3db`):

| Task | Measures | Read |
|---|---|---|
| `mote_visit.txt` | the idle face: lingering (mark 1-2), then a trip of half the ring (mark 2-3) | frame ms p50 / p95 / max, the trip's anim fps |
| `mote_hover.txt` | the sway beside an error (mark 1-2) | frame ms, anim fps (26 expected) |
| `mote_empty.txt` | the empty deck lit (mark 1-2) and ambient (mark 3-4) | frame ms |
| `mote_alert.txt` | a request during a trip (mark 1) | mark 1 to the first frame after it, and that frame's ms |

The boot move, the pairing and update flares, the replay and a big finish need a reboot, a pairing, an update,
an hour away or a done summary, which the bench cannot play; their frame times come from the timeline of a
session run by hand (`bench start`, do it, `bench stop`) until it can.
