---
id: 20261112-c6cf
title: VOICE: long transcripts cannot be sent (page them, then hold)
status: claimed
severity: med
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-12T22:53:05Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

A dictated reply longer than five lines on the VOICE screen cannot be sent: a tap or hold toasts "Too long to send from here" (ui_voice.c s_too_long). A 14 s dictation (45 words) hits it. The rule that the user must have seen all of what a hold sends is right, but the watch should let them read it: page the transcript (tap / A turns the page, like the ASK reader) and arm the hold once the last page has been on screen.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: a9cdcd0 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-12T22:53:05Z · copilot-cli/opus@mac-work · macos-arm64 · a9cdcd0: filed
- 2026-11-12T22:53:09Z · copilot-cli/opus@mac-work · macos-arm64 · a9cdcd0: claimed
- 2026-11-12T23:00:13Z · copilot-cli/opus@mac-work · macos-arm64 · 819843e: Commit 819843e: VOICE pages a transcript longer than five lines (tap/A turns the page, button 'More n/m' until the last page was shown, then the Send hold; an early hold toasts 'Tap to read the rest' and sends nothing). Daemon caps transcripts at 255 UTF-8 bytes (watch buffer) as well as 240 chars. Checks on macos-arm64: ctest 153/153 (sim_voice_long_transcript rewritten), idf.py build 1.39 MB (78% free), check_dram 184213 B free, atlas extract, cargo fmt/clippy/test --workspace. Daemon reinstalled under launchd. Not yet done: flash and live 14 s dictation (watch not on USB).
