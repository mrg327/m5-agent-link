---
id: 20261110-420c
title: Copilot CLI question forms (ask_user) can't be answered from the watch
status: fixed
severity: med
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-10T22:12:52Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

When Copilot CLI asks through its form tool (the hook sends Notification "Information requested"), the watch shows a question and plays the input sound, but the user can't answer it from the watch. Correction after investigating: the schema IS in the preToolUse payload. A form with one choice field was already answerable; the form in the f718 test had several fields (one free text), so it fell back to Focus-only. Fix: forms whose fields all have fixed choices get one card per field, matched to the tab Copilot is showing. Seen during the f718 sound test.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: fe042e2 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T22:12:52Z · copilot-cli/opus@mac-work · macos-arm64 · fe042e2: filed
- 2026-11-10T22:27:45Z · copilot-cli/opus@mac-work · macos-arm64 · a697c78: claimed
- 2026-11-10T22:29:29Z · copilot-cli/opus@mac-work · macos-arm64 · e7472ed+dirty: Captured screens: Copilot shows a multi-field form as tabs, one field at a time; Enter accepts and moves to the next tab, and on the last tab it submits. Yes/no fields show Yes/No with the cursor on the default. The schema is in the preToolUse payload. Plan: one watch card per choice field, in schema order (needs serde_json preserve_order).
- 2026-11-10T22:31:14Z · copilot-cli/opus@mac-work · macos-arm64 · e7472ed+dirty: Implemented ask_requests: one card per choice field, field-title screen check, sequential open_and_wait; serde_json preserve_order. cargo fmt/clippy/test clean. Awaiting live test.
- 2026-11-10T22:35:11Z · copilot-cli/opus@mac-work · macos-arm64 · f882978: fixed: commits 72c14bd, f882978; cargo fmt/clippy/test clean; live on watch: 2-field form (boolean + enum) answered from the watch, both typed, result matched the watch picks
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: f882978 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
