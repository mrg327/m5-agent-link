---
id: 20261109-c656
title: Board restart orphans the render and allows two at once
status: open
severity: med
area: board
platforms: any
owner:
created: 2026-11-09T17:02:26Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

The shim restarts server.mjs with SIGTERM; renderJob keeps running and the new server can start a second render into the same build/board/<sha>/.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:27Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
