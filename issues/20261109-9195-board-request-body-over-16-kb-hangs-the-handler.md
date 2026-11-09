---
id: 20261109-9195
title: Board request body over 16 KB hangs the handler
status: open
severity: low
area: board
platforms: any
owner:
created: 2026-11-09T17:02:27Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

readJson: req.destroy() fires close, not end, so the promise never settles; async handler errors are unhandled rejections.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:27Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
