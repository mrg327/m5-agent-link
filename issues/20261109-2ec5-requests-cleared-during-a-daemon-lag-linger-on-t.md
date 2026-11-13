---
id: 20261109-2ec5
title: Requests cleared during a daemon lag linger on the device
status: fixed
severity: low
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-09T17:14:24Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
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
- 2026-11-13T19:59:20Z · copilot-cli/opus@mac-work · macos-arm64 · 5470781: claimed
- 2026-11-13T20:02:45Z · copilot-cli/opus@mac-work · macos-arm64 · cb8cbaf+dirty: Daemon-only fix: Conn tracks rids sent on the link (insert on attention, remove on clear, reset at handshake); on broadcast lag it sends clear cancelled for sent rids no longer pending, after the snapshot and before re-sent pending. Test device::tests::a_lag_clears_the_requests_that_ended_meanwhile (fails without fix). PROTOCOL.md snapshot note. cargo fmt/clippy/test (310 pass). Not committed.
- 2026-11-13T20:04:25Z · copilot-cli/opus@mac-work · macos-arm64 · 0d9c5fd+dirty: fixed: commit 0d9c5fd; checks: cargo fmt, clippy, agentlinkd 310 passed (new a_lag_clears_the_requests_that_ended_meanwhile fails without the fix), extract --check
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 0d9c5fd+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
