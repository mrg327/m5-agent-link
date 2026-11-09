---
id: 20261109-da9d
title: Waking tap leaks into the UI (can answer an unread ASK)
status: open
severity: high
area: firmware
platforms: any
owner:
created: 2026-11-09T17:02:24Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

app.c dispatch() clears s_swallow_touch on the G_RELEASE of the waking contact; gesture.c finish_touch() then emits G_TAP/G_SWIPE_*/G_SCRUB_END, which reach ui_on_gesture. On a glow-revealed ASK a quick tap can commit a no-hold question option. sim test tap_wake_hold covers only a long hold. Expected: every event of the waking contact is swallowed.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:24Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
