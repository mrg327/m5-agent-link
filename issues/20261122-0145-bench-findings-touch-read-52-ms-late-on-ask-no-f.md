---
id: 20261122-0145
title: "Bench findings: screen changes are 3 full-screen frames at 13 fps, swipes show nothing until lift, no press state on a held disc, agent screens at 16 fps"
status: open
severity: med
area: firmware
platforms: esp32
owner:
created: 2026-11-22T09:16:07Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Measured with the bench (issue 20261122-bb2e, docs/design/BENCH.md §7) on the StopWatch over USB, 2026-11-22,
four tasks x3 runs, exact input:

- ASK: the polled touch is read about 52 ms after a stroke arrives, every time (approve and answer); the loop
  is busy drawing. Frames on the answered ASK are ~49 ms.
- Approve: after the finger lands on the hold disc the first frame comes ~290 ms later: nothing on screen says
  the press was felt for over a quarter second.
- Reply (AGENT, the Reply list): 16 fps in animation, median frame 37 ms against the 33 ms LVGL period, p95 74 ms.
- Switch (HOME rim drag): 30 fps, p95 frame 15 ms: the baseline to aim for.

Reproduce: see BENCH.md §7. The timelines are CSV per run; frame records carry pixels flushed per frame, which
should show which screens redraw too much.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: d9213f1 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T09:16:07Z · claude-code@linux-dev · linux-x86_64 · d9213f1: filed
- 2026-11-22T19:20:21Z · claude-code@linux-dev · linux-x86_64 · 3724645: Correction from the UX profile (docs/design/UX-PROFILE.md §1-2, branch ux-profile): the 52 ms touch-read delay on ASK was the dark display's 50 ms poll (the f724 bug turned the display off when the card arrived); with the display on, every screen reads the touch 11-13 ms after it lands. New: every screen change is three full-screen frames of 70-110 ms (ui_motion_enter: the content rises 20 px over 160 ms, so each step repaints all 217 k px), ~250 ms at 13 fps; a swipe draws nothing until the finger lifts (~450 ms to the finished screen); STATUS frames take 100-107 ms; a held disc shows no press state for ~320 ms (NAV_HOLD_SHOW_MS 250 by design, but the disc could change at touch-down).
