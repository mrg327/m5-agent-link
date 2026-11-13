---
id: 20261113-2e27
title: Jobs: the agentlink ssh tunnel shows on the watch as a third session
status: fixed
severity: low
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T22:38:54Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

agentlink ssh <remote> runs in a terminal for as long as the remote session lasts, so the shell integration surfaces it as a job card next to the remote agent's own card. ssh is on the ignored list, but the program here is agentlink.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 2a3a4d1+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T22:38:54Z · copilot-cli/opus@mac-work · macos-arm64 · 2a3a4d1+dirty: filed
- 2026-11-13T22:38:54Z · copilot-cli/opus@mac-work · macos-arm64 · 2a3a4d1+dirty: claimed
- 2026-11-13T22:38:54Z · copilot-cli/opus@mac-work · macos-arm64 · 9e1fc0d: fixed: commit 9e1fc0d; unit tests; live on mac-work: an injected 'agentlink ssh speech-win' start got no card while a 'sleep 45' control did
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 9e1fc0d main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
