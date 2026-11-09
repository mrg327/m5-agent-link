---
id: 20261109-a6b7
title: SCROLL over BLE: smoother lines, fast interval, daemon stuck after watch reset
status: fixed
severity: med
area: daemon
platforms: macos
owner:
created: 2026-11-09T18:05:05Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

Trace (USB) showed 1-2 line jumps every 40-50 ms and 5-9 during a glide, ~35 ms per kitten call: visibly steppy. Over BLE the link interval is the central's default (macOS: 30 ms) and nothing requested the interactive interval DESIGN §7 promises. Also: on macOS, after the watch resets, btleplug logs 'Event receiver died' and the BLE reader's is_connected() never returns, so the daemon never relinks.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: f2c56b1+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T18:05:05Z · copilot-cli/opus@mac-work · macos-arm64 · f2c56b1+dirty: filed
- 2026-11-09T18:05:15Z · copilot-cli/opus@mac-work · macos-arm64 · dd3c618: fixed: commits cd29321 (daemon) + dd3c618 (firmware). Verified on macOS + kitty 0.49.1: 2-line batches every 40 ms arrive as single lines ~21 ms apart, a 9-line batch spread over ~30 ms, main-screen positions exact; watch reset -> daemon relinks over BLE in ~4 s; watch logs 'connected: interval 30.00 ms'. Fast-interval request not yet observed on hardware (needs SCROLL opened by hand). sim ctest 47/47, atlas 90/90, wasm + idf builds OK, cargo clippy/test clean.
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: dd3c618 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
