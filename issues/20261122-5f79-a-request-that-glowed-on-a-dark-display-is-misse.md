---
id: 20261122-5f79
title: A request that glowed on a dark display is missed: the next wake shows the previous session, not the request
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §3 E3). A request on a dark display glows 2 s then the display goes dark; the next touch wakes HOME on the previously selected session (e.g. a Done card), not the request.
Repro: simulator/scripts/ux (branch ux-profile)/events/e22_glow_missed.txt (run with --idle-off 3).
Code: app.c on_attention returns before selecting when the display is off or glowing; dispatch reveals only while the glow is active. Should: a wake after an unseen buzz shows what buzzed (select it, or open ASK).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
