---
id: 20261113-c36c
title: STATUS toggles: the Quiet and Mute icons do not show the current state
status: claimed
severity: low
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T18:02:06Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Reported by the user 2026-11-13: on STATUS the Quiet toggle always shows a crossed-out bell (ICON_QUIET) and the Mute toggle a crossed-out speaker (ICON_MUTE), whatever the state; only the disc fill changes. So the bell is crossed out while alerts are on, and when they are off the disc is shaded but the cross looks the same as before.

Fix: each icon shows the current state. Quiet: bell (ICON_BELL) while alerts are on, bell-slash (ICON_QUIET) while quiet runs. Mute: speaker (ICON_SOUND) while sounds are on, speaker-slash (ICON_MUTE) while off or silent. The sky fill still marks the silencing state (ui_status.c render(), UI-V3 §4.6).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 7a19c7e main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T18:02:06Z · copilot-cli/opus@mac-work · macos-arm64 · 7a19c7e: filed
- 2026-11-13T18:02:10Z · copilot-cli/opus@mac-work · macos-arm64 · 7a19c7e: claimed
