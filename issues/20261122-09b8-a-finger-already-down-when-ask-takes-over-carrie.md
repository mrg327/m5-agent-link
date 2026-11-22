---
id: 20261122-09b8
title: A finger already down when ASK takes over carries in: its long press is ASK's back and closes the request unseen
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
