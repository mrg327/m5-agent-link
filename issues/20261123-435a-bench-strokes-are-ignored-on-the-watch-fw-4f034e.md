---
id: 20261123-435a
title: Bench strokes are ignored on the watch (fw 4f034e5); real touch works
status: open
severity: med
area: firmware
platforms: esp32
owner:
created: 2026-11-23T18:12:48Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

On the StopWatch with fw 4f034e5 (flashed 2026-11-23 over USB), `agentlink bench run bench/tasks/*.txt`
from a USB-only daemon built from main: every played stroke is ignored. 9 of 11 tasks fail
(agent_back, reply, status, watch, labs, arrival: "expected screen agent/…, the watch shows ring";
approve, deny, answer: "the watch did not send cmd …"). switch and busy pass only because their
checks are button actions. A real finger works (tap wakes, second tap opens AGENT, checked by hand).

## Evidence

- The watch logs `bench touch` for each stroke (n, dt, x, y correct), so dbg_touch arrives and
  bench_on_touch runs.
- The saved timeline (timeline-switch-1.csv) has no BENCH_DOWN/BENCH_UP records at all and one
  gesture (the button). So bench_touch() never reported a played stroke at a touch read, or the
  read never ran while a stroke was playing.
- The same bytes replayed in the simulator (`rx` of the bench's dbg_bench start and dbg_touch frames)
  open AGENT: the fault is on the device only.
- Different from the 2026-11-22 bench runs (BENCH.md §7, fw cb1eabc), which worked:
  - the watch had two BLE centrals linked (the service and another machine) beside the USB bench daemon;
  - every frame from the bench daemon logged `rx_id_not_monotonic` (last id 0x4000000x, the other
    link's ids).
  - touch_read_cb and bench.c look unchanged in substance since cb1eabc.

## Next

1. A debug build that logs each touch read while `bench_playing()`, to see whether the read runs.
2. Try with the BLE centrals off, to test the multi-link suspicion.
3. Then a sim test that plays bench strokes (`rx` dbg_touch), so this path is covered.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 4f034e5 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-23T18:12:48Z · claude-code@linux-dev · linux-x86_64 · 4f034e5: filed
