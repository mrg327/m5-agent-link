---
id: 20261113-203f
title: Delta OTA: the main loop stalls for seconds while a chunk rebuilds image bytes
status: open
severity: low
area: firmware
platforms: esp32s3
owner:
created: 2026-11-13T20:47:34Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

With a delta update (ef2c) a 2 KB patch chunk can rebuild hundreds of KB of image, all written to flash inside ota_on_chunk on the main loop. Live: the first chunks took 6.0 s and 4.4 s, so the UI freezes and acks lag. Progress is counted in patch bytes, so the screen jumps 0 -> 49 -> 99%. Ideas: bound the work per loop pass (feed the decoder in slices and ack when the slice is done), and show progress as image bytes written (s.out / s.size).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 3741845 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T20:47:34Z · copilot-cli/opus@mac-work · macos-arm64 · 3741845: filed
