---
id: 20261116-7847
title: agentlink hooks list / install <agent>: check and install agent hooks
status: claimed
severity: low
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-16T22:07:21Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Add `agentlink hooks list [agent]` (installed / partial / outdated / missing, per agent) and `agentlink hooks install <agent>` (copilot, claude, pi) that writes only when something is missing or stale, keeps a backup, and merges into Claude's settings.json without touching other hooks. DESIGN §6a.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 0b20c07 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-16T22:07:21Z · copilot-cli/opus@mac-work · macos-arm64 · 0b20c07: filed
- 2026-11-16T22:07:21Z · copilot-cli/opus@mac-work · macos-arm64 · 0b20c07: claimed
