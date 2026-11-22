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

```
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
LVGL and the gesture recognizer, so they see a finger. The panel's real touches are ignored while a stroke plays.
A stroke that arrives while another plays lifts the first one. A stroke counts as user activity: it wakes a dimmed
display, as a real touch does.

A debug link can already inject gestures (`dbg_inject_input`), so a stroke grants nothing new. It is accepted
only from a welcomed daemon, like every other debug message.

## 3. The timeline

`bench_start` allocates 4096 records (64 KB, PSRAM on the watch) once and clears them. Recording stops when the
buffer is full: the start of a run is kept and `lost` counts the rest. At 30 frames a second that is over two
minutes of animation.

Each record is 16 bytes, little-endian:

| Offset | Field | |
|---|---|---|
| 0 | `t` u32 | µs since `bench_start` |
| 4 | `a` u32 | frame: µs from render start to the end of the flush; other kinds: 0 |
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

```
wait MS | tap X Y | hold X Y MS | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long|double] | lift
session SID STATE REPO [BRANCH [SUMMARY]] | attention RID SID approval|question ACTS GRANT TITLE|TEXT|OPT...
mark N | expect_cmd OP | expect_screen NAME
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

## 7. Later

- Filmstrips: keep the changed area of N frames after a trigger in PSRAM and upload them, so an animation can be
  checked frame by frame.
- Pace the simulator by the costs the watch measures, so a simulator animation has the watch's frame rate.
- Many seeded runs of the user model in the simulator, for miss rates on small targets.
