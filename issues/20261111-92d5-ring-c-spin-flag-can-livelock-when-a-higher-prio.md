---
id: 20261111-92d5
title: ring.c spin flag can livelock when a higher-priority task preempts the holder on the same core
status: fixed
severity: low
area: firmware
platforms: esp32
owner: copilot-cli/opus@mac-work
created: 2026-11-11T21:53:16Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: macos-arm64
gh:
---

## Summary

`ring.c` and `app.c` (`s_rx_lock`) guard their FIFOs with a busy-wait atomic flag. On the device the pushers
are tasks of different priorities (usb_link at 5, the NimBLE host task, the main loop). If a task is preempted
while it holds the flag by a higher-priority task on the same core that wants the same flag, that task spins
forever (the holder never runs again) until the task watchdog resets the watch. The window is a few
instructions for the small rings and one message copy for the frame queue (shorter since the perf branch
copies only the message). Not seen in the field as far as the logs show. A FreeRTOS critical section
(portENTER_CRITICAL with a spinlock) or a mutex for the task-only rings would close it. Found while moving
the frame queue to PSRAM; not changed there, since it is a behaviour change outside that pass.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 5898b75 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T21:53:16Z · claude-code@linux-dev · linux-x86_64 · 5898b75: filed
- 2026-11-13T19:59:20Z · copilot-cli/opus@mac-work · macos-arm64 · 5470781: claimed
- 2026-11-13T20:15:28Z · copilot-cli/opus@mac-work · macos-arm64 · cfef7d9+dirty: fixed: commit cfef7d9; checks: sim ctest 188/188 (new threaded ring stress test fails if publish precedes copy), idf build ok, check_dram 182321 B free, atlas extract --check ok
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: cfef7d9+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
