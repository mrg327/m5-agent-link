---
id: 20261110-71da
title: Pare the AGENT wheel to <=3 items; remove SCROLL mode
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-10T05:19:05Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User decision 2026-11-09: WATCH made "Scroll terminal" redundant. Remove the SCROLL mode from the
firmware (screen, gestures, nav, tests, atlas), keep the daemon's scroll support so it can return.
Drop "Status…" (swipe down on HOME) and "Focus terminal" (button A on HOME and WATCH) from the
wheel. Items: Working = Watch · Notify when done · Snooze; Done = Mark seen · Watch;
Error = Dismiss · Watch; Idle = Snooze; Input = Focus (until quick replies land).
Update UI-V2.md §2–§4, the gesture map, atlas facts.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 9d39d6d main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-10T05:19:05Z · claude-code@linux-dev · linux-x86_64 · 9d39d6d: filed
- 2026-11-10T05:19:11Z · claude-code@linux-dev · linux-x86_64 · 9d39d6d: claimed
