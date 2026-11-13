---
id: 20261113-61ce
title: Experiments menu (swipe up on HOME) with a trackpad that drives the host pointer
status: fixed
severity: med
area: protocol
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T18:55:23Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

Swipe up on HOME opens an Experiments menu (tap keeps opening AGENT/ASK). First item: Trackpad.
- centre drag moves the pointer (2D, coalesced, accelerated)
- tap = left click, double tap = double click, hold still = right click
- bezel scrub clockwise = scroll down, counter-clockwise = up (one notch per ~15 degrees)
Needs a new `mouse` cmd op (proto minor bump), a daemon pointer backend (macOS CGEvent with the
Accessibility permission; Linux/Windows later), sim scripts, and docs/design/EXPERIMENTS.md.
Drag (tap then hold and move) is deferred.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: d4fa4c2 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T18:55:23Z · copilot-cli/opus@mac-work · macos-arm64 · d4fa4c2: filed
- 2026-11-13T18:55:27Z · copilot-cli/opus@mac-work · macos-arm64 · d4fa4c2: claimed
- 2026-11-13T19:20:23Z · copilot-cli/opus@mac-work · macos-arm64 · 95d85ba: firmware 95d85ba: LABS + Trackpad screens, G_MOVE, link_send_pointer; checks: sim ctest 188/188, idf build + check_dram (182 KB free), atlas --check + node 216/216
- 2026-11-13T19:28:21Z · copilot-cli/opus@mac-work · macos-arm64 · a109c12: fix a109c12: bezel scroll jumped back (wrap at 9 sectors on a 24-sector rim); checks: sim 188/188, idf build + check_dram, atlas
- 2026-11-13T19:33:48Z · copilot-cli/opus@mac-work · macos-arm64 · ed9c889: fixed: user confirmed on the watch, 2026-11-13
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: ed9c889 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
