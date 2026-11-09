---
id: 20261109-ce9e
title: Scroll delta overflow on malformed device input
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

scroll.rs Coalescer::push: last.delta + b.delta on an unclamped device i64 overflows (panic in debug, wrong sign in release).

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
- 2026-11-09T17:04:24Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 830f290+dirty: fixed: checks: fmt, clippy, cargo test --workspace green; saturating_add in coalescer, unsigned_abs in kitty scroll_amount, i128 math in Tracked::apply; tmux already used unsigned_abs; tests with i64::MAX/MIN
    - agent: copilot-cli/sonnet-daemon
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 830f290+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
- 2026-11-09T17:13:57Z · copilot-cli/opus@mac-work · macos-arm64 · 75f780b+dirty: committed in 1c910ee; orchestrator re-ran cargo fmt/clippy -D warnings/test (67 pass) on macos-arm64
