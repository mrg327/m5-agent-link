---
id: 20261122-761f
title: UX profile: every screen and flow on the watch, edge cases and UI gaps, from the user's side
status: claimed
severity: med
area: firmware
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-22T18:52:16Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
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
