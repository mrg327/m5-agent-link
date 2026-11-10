---
id: 20261110-5cbb
title: Scroll the terminal by dragging on WATCH
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-10T07:04:53Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

User report 2026-11-09 after 71da: "Scrolling no longer works. When an agent is running it is not an
option in the watch area. The user should always be able to scroll the screen." Decision: a vertical
drag in the centre of WATCH scrolls that session's terminal (the trackpad SCROLL of Phase 4: drag,
glide, A = to bottom); leaving SCROLL returns to WATCH. Swipe up no longer leaves WATCH (B held or
A+B do). No wheel item. Restores the SCROLL code removed in 4bfa916.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 8cc2598 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-10T07:04:53Z · claude-code@linux-dev · linux-x86_64 · 8cc2598: filed
- 2026-11-10T07:04:53Z · claude-code@linux-dev · linux-x86_64 · 8cc2598: claimed
- 2026-11-10T15:43:07Z · claude-code@linux-dev · linux-x86_64 · e57dec3+dirty: fixed: this commit; ctest 60/60 (scroll_mode, scroll_momentum, scroll_exit, scroll_privacy, scroll_unsupported back, entered by a drag on WATCH; watch.txt checks drag -> SCROLL -> B held -> WATCH -> B held -> deck); idf.py build ok; atlas facts regenerated, facts_schema passes. Verified on linux-x86_64 in the simulator; not yet on the watch.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: e57dec3+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
