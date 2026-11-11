---
id: 20261111-3aa6
title: Performance pass: internal RAM, firmware size, hook latency, dev daemon (branch perf)
status: claimed
severity: low
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-11T21:53:15Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

A measured pass over the current software (branch `perf`, on top of `jobs`), no behaviour change:
- 57 KB of plain message buffers moved from internal RAM to PSRAM (check_dram 127377 -> 184985 B free).
- The frame queue copies only the message, not its 4 KB slot.
- LVGL built with only what the UI uses: agentlink.bin 1343344 -> 1167088 B, LVGL heap 76.7 -> 68.5 KB (sim).
- agentlink-hook reads /proc instead of running `ps -A`: 9.6 -> 1.2 ms per call on Linux.
- Cargo dev profile optimises dependencies and agentlink-keys: the swipe decoder's tests 10.4 -> 0.8 s.
Verified: ctest 98/98, cargo test, 106 simulator screenshots against the previous build, and a flash with a
clean boot log and the BLE link up. Left alone, measured and judged fine: redraw budgets, the main loop, the
transition and comet animations.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 5898b75 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T21:53:16Z · claude-code@linux-dev · linux-x86_64 · 5898b75: filed
- 2026-11-11T21:53:16Z · claude-code@linux-dev · linux-x86_64 · 5898b75: claimed
