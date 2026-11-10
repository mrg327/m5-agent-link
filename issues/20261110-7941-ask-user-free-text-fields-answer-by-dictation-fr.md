---
id: 20261110-7941
title: ask_user free-text fields: answer by dictation from the watch, or open on PC
status: fixed
severity: med
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-10T22:37:21Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

A Copilot ask_user form field without fixed choices (free text) gets a Focus-only card. The user wants such fields answerable by dictation from the watch, with Open on PC as the alternative. Needs a dictate action on attention cards (protocol), Dictate... on the ASK screen (firmware), and the daemon routing the transcript to that card and typing it into the form field (prompt.rs). Follows 420c.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: f6473c1 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T22:37:22Z · copilot-cli/opus@mac-work · macos-arm64 · f6473c1: filed
- 2026-11-10T22:37:25Z · copilot-cli/opus@mac-work · macos-arm64 · f6473c1: claimed
- 2026-11-10T22:44:52Z · copilot-cli/opus@mac-work · macos-arm64 · 63d5f73+dirty: daemon: text-field cards (Expect::Text, type_text with field/empty-box/echo checks), dictate rid answer via broker.answer_text, per-device Dictate gating (minor>=8 + voice), postToolUse toolName fix; protocol 0.8 action_dictate + vectors; daemon checks green. Firmware in progress.
- 2026-11-10T23:09:17Z · copilot-cli/opus@mac-work · macos-arm64 · 39fcc83: fixed: commit 39fcc83; checks: daemon fmt/clippy/test, protocol vectors, sim ctest, device build + check_dram, atlas; live: dictated note + choice on a 2-field form answered from watch
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 39fcc83 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
