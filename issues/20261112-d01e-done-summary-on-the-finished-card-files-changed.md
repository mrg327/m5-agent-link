---
id: 20261112-d01e
title: Done summary on the finished card: files changed, tests, time taken
status: claimed
severity: med
area: protocol
platforms: any
owner: claude-code@linux-dev
created: 2026-11-12T08:04:20Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
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
