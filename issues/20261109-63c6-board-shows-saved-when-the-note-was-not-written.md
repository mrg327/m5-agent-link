---
id: 20261109-63c6
title: Board shows Saved when the note was not written
status: fixed
severity: med
area: board
platforms: any
owner: copilot-cli/sonnet-board@mac-work
created: 2026-11-09T17:02:26Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

server.mjs saveComments() swallows write errors; POST /comments returns 200 so the page says saved while the note lives only in memory until the next restart.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:26Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:03:34Z · copilot-cli/sonnet-board@mac-work · macos-arm64 · 830f290: claimed
- 2026-11-09T17:04:26Z · copilot-cli/sonnet-board@mac-work · macos-arm64 · 830f290+dirty: fixed: checks: node --check; scratch-tree harness: POST /comments ok, unwritable dir -> 500 with reason and note absent from GET, delete failure -> 500 and note kept; board.html shows server error
    - agent: copilot-cli/sonnet-board
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 830f290+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
- 2026-11-09T17:13:57Z · copilot-cli/opus@mac-work · macos-arm64 · 75f780b+dirty: committed in 61369e5
