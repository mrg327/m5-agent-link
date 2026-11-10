---
id: 20261109-d211
title: Watch scroll for terminals without a scroll API (Terminal.app, iTerm, Windows Terminal)
status: open
severity: low
area: daemon
platforms: macos,windows
owner:
created: 2026-11-09T17:43:19Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

SCROLL works in kitty (wheel kitten, a059) and tmux only; other terminals return Unsupported. Possible route: synthesized OS wheel events at the terminal window (macOS CGEvent scroll-wheel, needs Accessibility permission; Windows SendInput MOUSEEVENTF_WHEEL). Must target the focused session's window only and never inject device text (DESIGN §9).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 32ae514 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:43:19Z · copilot-cli/opus@mac-work · macos-arm64 · 32ae514: filed
- 2026-11-10T05:19:11Z · claude-code@linux-dev · linux-x86_64 · 9d39d6d: Heads-up: per user decision 2026-11-09 the firmware SCROLL mode is being removed (issue 71da); the daemon keeps its scroll support. Watch scroll for other terminals has no device entry point after that, so this is on hold unless SCROLL returns.
