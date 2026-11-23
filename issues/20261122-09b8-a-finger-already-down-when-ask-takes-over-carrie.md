---
id: 20261122-09b8
title: A finger already down when ASK takes over carries in: its long press is ASK's back and closes the request unseen
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code/takeover@linux-dev
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §3 E4). With a finger resting on SCROLL or TRACKPAD, a request arrives and ASK opens; the same contact's 600 ms long press is ASK's back (off the rows), so ASK closes ~0.5 s after it appears.
Repro: simulator/scripts/ux (branch ux-profile)/events/e11_carry_longpress.txt.
Code: app.c dispatch: s_swallow_touch is set on a wake, not when a mode change happens under the finger. Should: swallow the rest of a contact that began before an automatic mode change.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
- 2026-11-23T07:05:43Z · claude-code/takeover@linux-dev · linux-x86_64 · ab35617+dirty: claimed
- 2026-11-23T07:05:50Z · claude-code/takeover@linux-dev · linux-x86_64 · ab35617+dirty: core_step: a mode change made by the link (drain_link) or by a screen closing itself (ui_tick) while a finger is down sets s_swallow_touch, as a wake does (swallow_if_moved). Buttons held across the change are not swallowed (out of scope here).
- 2026-11-23T07:25:17Z · claude-code/takeover@linux-dev · linux-x86_64 · 305e034: fixed: commit 405a679; checks: scripts/check format structure docs lint atlas sim firmware PASS (sim 236/236 with the virtual clock 4b308a0); regression test sim_takeover_finger_carry
    - agent: claude-code/takeover
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: 305e034 worktree-agent-a5e73b515ae79c7ff
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
