---
id: 20261122-bb2e
title: Bench: drive the real watch like a finger and measure frames and task times
status: claimed
severity: med
area: tools
platforms: any
owner: claude-code@linux-dev
created: 2026-11-22T08:14:52Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Today the simulator replays real touch input but with desktop timing, and the watch has real timing but
only post-recognition gesture injection (`agentlink device tap/press/lift` are stubs) and a one-number
fps. Nothing measures what a user feels: frame times during an animation, touch-to-frame latency, or
how long a task takes from the first touch.

Plan (docs/design/BENCH.md):
1. Raw touch strokes sent to the watch (debug message), replayed into the touch read path so LVGL and
   the gesture recognizer see a finger.
2. A per-frame timeline on the watch (frame time, panel wait, pixels, touch and gesture marks) dumped
   after a run.
3. A bench mode in the daemon: hold back real sessions, show synthetic ones, answer the watch's
   commands itself; `agentlink bench run SCRIPT` plays a task and writes a report.
4. Four task scripts (approve, answer, reply, switch) with a simple user model (reaction time, tap
   scatter), runnable in the simulator and on the watch. Report only, no gate. No camera.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: afdd71a main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T08:14:52Z · claude-code@linux-dev · linux-x86_64 · afdd71a: filed
- 2026-11-22T08:14:53Z · claude-code@linux-dev · linux-x86_64 · afdd71a: claimed
- 2026-11-22T08:34:56Z · claude-code@linux-dev · linux-x86_64 · 4721000: Stage 1 on branch bench (89f29b6): proto 0.24 dbg_touch/dbg_bench/dbg_frames, stroke player + frame timeline in core (bench.c), sim commands touch/bench/expect_cmd. ctest 217/217, firmware builds (+88 B internal RAM), cargo 414, atlas 230. Next: bench mode in the daemon and agentlink bench run.
