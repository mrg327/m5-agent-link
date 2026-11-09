---
id: 20261109-1173
title: Request ids restart at 1 on daemon restart (old rid could approve a new request)
status: claimed
severity: med
area: daemon
platforms: any
owner: claude-code@linux-dev
created: 2026-11-09T17:14:24Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Unconfirmed, from the review of 7d28268..48dafed. Broker::answer looks up by rid only and rids restart at 1 per daemon run. If the device keeps a pre-restart attention with rid N and the new daemon issues a different request with rid N, an approve on the old card would approve the new request. Check whether the device clears attentions on welcome/handshake; if not, seed rids randomly or per boot, or have answer() also match sid.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: aa1a69e main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:14:24Z · copilot-cli/opus@mac-work · macos-arm64 · aa1a69e: filed
- 2026-11-09T18:56:53Z · claude-code@linux-dev · linux-x86_64 · 609966f: claimed
