---
id: 20261112-d01e
title: Done summary on the finished card: files changed, tests, time taken
status: fixed
severity: med
area: protocol
platforms: any
owner: claude-code@linux-dev
created: 2026-11-12T08:04:20Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

When an agent finishes a turn the card shows what it did at a glance: files changed, lines added and removed, test result when known, time taken. Touches protocol, daemon and firmware. Branch done-summary.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 997305d HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-12T08:04:20Z · claude-code@linux-dev · linux-x86_64 · 997305d: filed
- 2026-11-12T08:04:21Z · claude-code@linux-dev · linux-x86_64 · 997305d: claimed
- 2026-11-12T08:52:47Z · claude-code@linux-dev · linux-x86_64 · 3680681: Built on branch done-summary, merged into next (de49ee9, a4a372f, 09aaf62): protocol 0.14 session keys 30-34 (fc, la, lr, tp, dur); per-turn tally in the daemon; one summary line on the finished card; state time reads as an age. Printed hook config now registers PostToolUseFailure and StopFailure. Merged tree: ctest 153/153, cargo test 311, six e2e scripts pass. Firmware 09aaf62 flashed, clean boot. Daemon side not deployed yet.
- 2026-11-16T22:37:13Z · copilot-cli/opus@mac-work · macos-arm64 · a2deb4a: macOS evidence (mac-work, daemon + firmware from main 0253ee0 over USB): a live pi turn (same per-turn tally, issue f705) reached the watch as done with fc 1, la 1, lr 1, dur 15 plus the last message; no frame rejected. No Claude Code sessions on this Mac, so the Claude payload path is still Linux-verified only.
- 2026-11-21T23:15:47Z · claude-code@linux-dev · linux-x86_64 · dc17059+dirty: fixed: Merged into main; live pi turn on macOS reached the watch with fc/la/lr/dur (2026-11-16); Claude Code payload path covered by Linux tests. main dc17059 (glyphs merged), flashed over USB 2026-11-21, clean boot log, BLE relinked; ctest 215/215, check_dram 185553 B free, atlas 224/224, cargo test 413 pass (one flaky first run, clean on rerun)
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: dc17059+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
