---
id: 20261113-c2fd
title: Quiet until I turn it off, and volume from the daemon
status: open
severity: low
area: firmware
platforms: any
owner:
created: 2026-11-13T18:17:56Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Left over from 20261111-4843 (volume landed in 3b81ba1): (1) Quiet on STATUS cycles off -> 30 min -> 1 h -> off; add 'until I turn it off' (and maybe a schedule). (2) The volume pref lives only on the watch: report it to the daemon and let cfg / agentlink set it. See the mute-model review logged on 4843 for the wider options.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 3b81ba1 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T18:17:56Z · copilot-cli/opus@mac-work · macos-arm64 · 3b81ba1: filed
