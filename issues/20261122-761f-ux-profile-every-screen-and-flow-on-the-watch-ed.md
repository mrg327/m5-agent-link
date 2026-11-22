---
id: 20261122-761f
title: UX profile: every screen and flow on the watch, edge cases and UI gaps, from the user's side
status: fixed
severity: med
area: firmware
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-22T18:52:16Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

User request 2026-11-22: a deep profiling of the watch from the user's perspective, looking for edge cases and gaps in the UI.

Plan: map every screen, input and transition; extend the bench (BENCH.md) to every flow and run it on the watch, exact and with the user model; stress the UI in the simulator with edge-case content and event orders (empty, many, long, unicode, interrupting cards, link loss mid-task); read each UI module for unhandled states. Output: a report (docs/design/UX-PROFILE.md) and one issue per finding.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: e830a6d main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T18:52:16Z · claude-code@linux-dev · linux-x86_64 · e830a6d: filed
- 2026-11-22T18:52:21Z · claude-code@linux-dev · linux-x86_64 · e830a6d: claimed
- 2026-11-22T19:21:00Z · claude-code@linux-dev · linux-x86_64 · 6d07365: Done on branch ux-profile (6c32527): docs/design/UX-PROFILE.md, 7 new bench tasks (11 pass on the watch, exact and --human 1), repro scripts in simulator/scripts/ux. Fixed f724 on the way (b980ef1, flashed). Filed 883f, 4b25, 5f79, 09b8, 6b60, 9689, 9df4, 67d1, 4094, d379; corrected 0145. Checks: ctest 229/229, cargo fmt/clippy.
- 2026-11-22T20:39:33Z · claude-code@linux-dev · linux-x86_64 · c1a491c: fixed: merged into main (c1a491c): docs/design/UX-PROFILE.md, findings filed as 883f 4b25 5f79 09b8 6b60 9689 9df4 67d1 4094 d379, 0145 corrected; ctest 229/229
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: c1a491c main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
