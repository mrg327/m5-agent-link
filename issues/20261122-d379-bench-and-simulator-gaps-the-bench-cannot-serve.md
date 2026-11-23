---
id: 20261122-d379
title: Bench and simulator gaps: the bench cannot serve READ/GRAPH/SCROLL content or errors; sim rx cut at ~255 bytes
status: claimed
severity: low
area: daemon
platforms: any
owner: claude-code/strings@linux-dev
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §6). The bench player has no drop, clear, result err, last-message, graph or scrollback steps, so those screens and the failure paths are measured only in the simulator. The simulator's script line parser copies into arg[512] and rx hex is cut at ~255 bytes (sim_script.c parse_line), so long text cannot take the real decode path in a script. The simulator cannot show timing bugs like f724 (BENCH.md §8 "paced simulator").

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:14Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
- 2026-11-23T06:49:17Z · claude-code/strings@linux-dev · linux-x86_64 · ab35617: claimed
