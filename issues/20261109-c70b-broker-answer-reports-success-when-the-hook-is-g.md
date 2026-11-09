---
id: 20261109-c70b
title: Broker answer() reports success when the hook is gone
status: fixed
severity: low
area: daemon
platforms: any
owner: copilot-cli/sonnet-daemon@mac-work
created: 2026-11-09T17:02:26Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

broker.rs answer(): result of p.tx.send is ignored; after the hook timed out the device gets ok and an approved audit line though nothing was approved.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:26Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:03:36Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290: claimed
- 2026-11-09T17:04:23Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290+dirty: device.rs caller: Err((Stale,'hook gone')) goes through fail(); audit line records the failed result, no 'answered from device' trace. Sensible.
- 2026-11-09T17:04:23Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290+dirty: fixed: checks: fmt, clippy, cargo test --workspace green; new broker test answer_with_the_hook_gone_is_stale_but_still_clears
    - agent: copilot-cli/sonnet-daemon
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 830f290+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
