---
id: 20261109-6c14
title: Answer Copilot CLI prompts from the watch
status: open
severity: med
area: daemon
platforms: macos
owner:
created: 2026-11-09T21:01:02Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Copilot hooks cannot return an answer, so its permission prompt and ask_user form are answered by typing into the agent's kitty window after a screen check (DESIGN §6a, agentlinkd/src/prompt.rs). Verified live on the watch: permission Yes, ask_user single-choice, a two-field form gets a Focus-only card, and Esc in the terminal clears the card. Still to verify: a remote session through its agentlink ssh window (Windows and WSL), and Copilot TUI changes in newer versions.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 95e1db1 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T21:01:02Z · copilot-cli/opus@mac-work · macos-arm64 · 95e1db1: filed
