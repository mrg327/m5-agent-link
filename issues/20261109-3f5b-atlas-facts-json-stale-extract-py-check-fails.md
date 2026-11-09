---
id: 20261109-3f5b
title: atlas facts.json stale (extract.py --check fails)
status: fixed
severity: med
area: atlas
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-09T17:02:27Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

gesture_map.wake_swallow points at app.c:300, now app.c:317. CI atlas job fails.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:27Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:13:56Z · copilot-cli/opus@mac-work · macos-arm64 · 75f780b+dirty: claimed
- 2026-11-09T17:13:56Z · copilot-cli/opus@mac-work · macos-arm64 · 75f780b+dirty: fixed: regenerated in 75f780b; extract.py --check OK, node --test 89/89
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 75f780b+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74
