---
id: 20261110-0ea9
title: Voice dictation from the watch (mic -> PC transcription -> hold to send)
status: fixed
severity: low
area: firmware
platforms: esp32
owner: claude-code@linux-dev
created: 2026-11-10T05:19:05Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: macos-arm64
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
- 2026-11-10T05:49:50Z · claude-code@linux-dev · linux-x86_64 · 3f29b0f: claimed
- 2026-11-10T16:30:30Z · claude-code@linux-dev · linux-x86_64 · 88facad: Built end to end in 88facad: mic.cpp, voice.c, ui_voice.c, voice.rs, proto 0.6. Checks: ctest 64/64 incl. sim_voice_dictate; daemon 121 incl. real speech through the watch's ADPCM block format and whisper.cpp base.en (2.5 s for 11 s); idf.py build + check_dram (132 KB free). Still to verify on the watch: the ES8311 init sequence (from esp_codec_dev, not checked on this board) and the I2S slot/levels. Staying claimed until then.
- 2026-11-10T20:36:13Z · copilot-cli/opus@mac-work · macos-arm64 · fbdd2d3: fbdd2d3: mic crash on the watch was a NULL 32 KB stream buffer from internal RAM; moved to PSRAM. Mic failures now reach the daemon log; reset cause reported each boot; whisper warmed (cold ~5 s, warm ~0.3 s). Dictation verified on hardware.
- 2026-11-10T20:43:36Z · copilot-cli/opus@mac-work · macos-arm64 · f5df473: fixed: Closed on the user's request. fbdd2d3: crash fixed (32 KB stream buffer moved to PSRAM), mic errors reported to the daemon, whisper warmed. Verified on the StopWatch: 3.3 s of speech transcribed in 236 ms and typed after hold.
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: f5df473 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
