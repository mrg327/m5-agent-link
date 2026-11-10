---
id: 20261110-0ea9
title: Voice dictation from the watch (mic -> PC transcription -> hold to send)
status: open
severity: low
area: firmware
platforms: esp32
owner:
created: 2026-11-10T05:19:05Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User decision 2026-11-09: after quick replies. StopWatch has an ES8311 codec and a MEMS mic
(board.cpp has_mic=false, no driver). Record on the watch, stream audio to the daemon over USB,
transcribe locally on the PC, show the text on the watch, hold to type it into the session.
Needs: codec + I2S driver, audio rail, a protocol stream, daemon STT, a confirm screen.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 9d39d6d main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-10T05:19:05Z · claude-code@linux-dev · linux-x86_64 · 9d39d6d: filed
