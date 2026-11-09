---
id: 20261109-f545
title: SCROLL should feel like a trackpad: fine steps, immediate send, momentum
status: claimed
severity: med
area: firmware
platforms: any
owner: copilot-cli/sonnet-fw@mac-work
created: 2026-11-09T17:25:13Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

SCROLL feels like "enter and send": 24 px quantised steps of 3 lines, the first command waits out the
50 ms coalesce window, and a flick jumps a whole page. The user wants it to behave like a trackpad or
scroll wheel: the terminal moves with the finger, immediately, and keeps gliding after a flick.

Fix: 1 line per ~10 px of drag (gated by tap slop so a tap still exits), the first step of a burst
sent at once (leading edge) and the rest coalesced, a detent per 3 lines (one wheel notch) instead
of per step, and momentum after release (exponential decay; a new touch stops it, as do the ends of
the scrollback). Flick no longer means "page"; button B still pages.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 955c3ab main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:25:13Z · copilot-cli/opus@mac-work · macos-arm64 · 955c3ab: filed
- 2026-11-09T17:25:16Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 955c3ab: claimed
