---
id: 20261109-6c14
title: Answer Copilot CLI prompts from the watch
status: claimed
severity: med
area: daemon
platforms: macos
owner: copilot-cli/opus@mac-work
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
- 2026-11-09T21:03:06Z · copilot-cli/opus@mac-work · macos-arm64 · 0aa3bf4: Driven with the headless simulator as a second device on ws://127.0.0.1:7878 against a live Copilot 1.0.106 session in kitty: permission card held Yes (hold_ms 1208), the daemon typed 1, and the command ran; ask_user card, B then tap on green, typed and Copilot reported green; a two-field form got a Focus-only card, and Esc in the terminal cleared it (answered_pc).
- 2026-11-13T19:34:08Z · copilot-cli/opus@mac-work · macos-arm64 · 204d2ff: claimed
- 2026-11-13T19:37:15Z · copilot-cli/opus@mac-work · macos-arm64 · 66d92a8: claimed; investigating the remaining checks (prompt answers through an agentlink ssh window, newer Copilot TUI)
- 2026-11-13T19:37:53Z · copilot-cli/opus@mac-work · macos-arm64 · 126c9c0: Reviewed: remote hook lines reach prompt::on_event like local ones, and type_answer/read go through registry.route -> the live attach's local kitty window (the agentlink ssh window); without an attach the card says 'remote session without its agentlink ssh window'. Since 029e the attach also survives a daemon restart. Left: a live permission prompt + ask_user through agentlink ssh to speech-win (Windows conpty glyphs) and sdv-pc-wsl, and re-check on Copilot 1.0.111 (released 2026-11-13; verified on 1.0.106).
