---
id: 20261110-71da
title: Pare the AGENT wheel to <=3 items; remove SCROLL mode
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-10T05:19:05Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
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
- 2026-11-10T05:29:32Z · claude-code@linux-dev · linux-x86_64 · ae799ae+dirty: fixed: this commit; ctest 54/54 (scroll_* tests removed with the mode; actions_wheel, agent_items, status, watch*, finished_buzz, center_tap_focus, perf_* updated for the new wheel); idf.py build ok; atlas extract --check ok, facts_schema updated (4 atlas files still fail on Node 18 as before, f95e). Verified on linux-x86_64, simulator and device build; not flashed yet.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: ae799ae+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
