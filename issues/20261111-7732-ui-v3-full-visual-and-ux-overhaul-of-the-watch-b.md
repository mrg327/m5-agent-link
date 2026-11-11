---
id: 20261111-7732
title: UI v3: full visual and UX overhaul of the watch (branch ui-v3)
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-11T05:14:58Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User request 2026-11-10: the idea is right but the UI and UX lack polish. Audit every screen, then
rework the look and the interaction details to the standard of a first-party consumer wearable:
simple, intuitive, useful, with no loss of function.

Scope (branch `ui-v3`, worktree beside the main checkout, nothing merged until the user has reviewed
before/after sheets):
- Audit: docs/design/UI-PROFILE.md (every screen rendered in the simulator, findings by severity).
- Spec: docs/design/UI-V3.md (type, colour, icon and motion tokens; each screen; gesture changes).
- Presentation layer: ui_theme, ui_ring*, ui_wheel, ui_home*, ui_agent, ui_ask, ui_watch, ui_scroll,
  ui_status, ui_voice, ui.c (toast, transitions); generated fonts and icons.
- Interaction layer: nav.c only for one universal "back" and direct row taps. Every safety invariant
  of UI-V2 §1 stays (tap never approves, hold bound to rid, 300 ms arm, strict deny swipe, first
  touch after glow only reveals, haptic triple).
- Simulator scripts, Atlas facts and docs follow the code.

Files other issues own are left alone: ui_keys.c / keys.c (1e57) get token-level changes only, and
voice.c, sim_link.c, sim_script.c (f62b) are not edited on this branch. Issue 4843 (settings: volume,
quiet) overlaps the STATUS screen; the redesigned STATUS leaves room for a volume control and 4843
should build on it.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: ff33ff3+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T05:14:59Z · claude-code@linux-dev · linux-x86_64 · ff33ff3+dirty: filed
- 2026-11-11T05:15:03Z · claude-code@linux-dev · linux-x86_64 · ff33ff3+dirty: claimed
