---
id: 20261109-371b
title: Remote sessions over SSH from Windows machines (REMOTE.md)
status: open
severity: med
area: daemon
platforms: windows,macos
owner:
created: 2026-11-09T18:11:16Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Implement docs/design/REMOTE.md §6: separate remote.sock listener with token auth, hook AGENTLINK_REMOTE=tcp: loopback transport on Windows, host index + host table, focus/scroll routed to the local SSH window (agentlink ssh wrapper) else unsupported, live test Mac + Windows 11 OpenSSH Server.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 58fc232+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T18:11:17Z · copilot-cli/opus@mac-work · macos-arm64 · 58fc232+dirty: filed
