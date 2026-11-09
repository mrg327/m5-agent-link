---
id: 20261109-2ec5
title: Requests cleared during a daemon lag linger on the device
status: open
severity: low
area: firmware
platforms: any
owner:
created: 2026-11-09T17:14:24Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Found while fixing 20261109-1d5b. After a broadcast lag the daemon now resends a snapshot plus broker.pending(), but firmware on_snapshot -> model_replace_all only drops attentions whose session vanished. A request cleared during the lag for a live session stays until it expires or is answered. Options: treat a snapshot as authoritative for attentions (drop rids not re-sent), or add an explicit pending-set message to the protocol.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: aa1a69e main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:14:24Z · copilot-cli/opus@mac-work · macos-arm64 · aa1a69e: filed
