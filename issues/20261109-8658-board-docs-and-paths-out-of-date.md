---
id: 20261109-8658
title: Board docs and paths out of date
status: open
severity: low
area: docs
platforms: any
owner:
created: 2026-11-09T17:02:27Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

BOARD.md names render.sh (it is render.mjs, which renders every sim script with shot; states.json is labels only). board.mjs channel instructions hardcode /home/matt/... for ctest.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:27Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
