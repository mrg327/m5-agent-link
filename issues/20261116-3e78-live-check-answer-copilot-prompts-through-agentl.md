---
id: 20261116-3e78
title: Live check: answer Copilot prompts through agentlink ssh (speech-win, sdv-pc-wsl) and on newer Copilot TUIs
status: open
severity: low
area: daemon
platforms: windows,linux
owner:
created: 2026-11-16T22:37:14Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Follow-up of 20261109-6c14 (closed for local kitty). Needs a person at the watch: a permission prompt and an ask_user form in a remote Copilot session opened with agentlink ssh to speech-win (Windows conpty glyphs) and to sdv-pc-wsl, answered from the watch. Also re-check the screen parsing when Copilot moves past 1.0.106 (prompt.rs depends on the TUI layout).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: a2deb4a main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-16T22:37:14Z · copilot-cli/opus@mac-work · macos-arm64 · a2deb4a: filed
