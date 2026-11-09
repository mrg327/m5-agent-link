---
id: 20261109-1d5b
title: Daemon drops pending attentions after a broadcast lag
status: fixed
severity: med
area: daemon
platforms: any
owner: copilot-cli/sonnet-daemon@mac-work
created: 2026-11-09T17:02:25Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

device.rs serve(): the RecvError::Lagged arm resends only the session snapshot; missed Attention/Clear updates are lost so the device can miss or keep showing a request.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:25Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:03:36Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290: claimed
- 2026-11-09T17:04:22Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290+dirty: Firmware finding: on_snapshot -> model_replace_all replaces sessions and drops only attentions whose session vanished; attentions cleared during the lag for a still-present session linger on the device until expiry/answer (daemon cannot clear them via snapshot). Resending pending attentions also re-sends already-shown ones (see 7dc6). No device.rs test added: serve() needs a live Conn/transport and a forced broadcast overflow; no existing harness for it.
- 2026-11-09T17:04:22Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290+dirty: fixed: checks: cargo fmt --check, clippy -D warnings, cargo test --workspace green; serve() Lagged arm now resends broker.pending() after the snapshot
    - agent: copilot-cli/sonnet-daemon
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 830f290+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
