---
id: 20261112-f97a
title: VOICE: keep and show long dictations (2 KB transcript, 60 s recording)
status: fixed
severity: med
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-12T23:55:30Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

Follow-up to c6cf. A dictation is cut to 240 chars / 255 bytes by the daemon (voice.rs clean) and the hold types the cut text, so anything past ~15 s of speech is lost, and recording stops at 15 s.

Plan (agreed with the user):
- proto 0.16: a device that announces it holds a transcript of up to 2047 UTF-8 bytes (PSRAM, like detail.c); older devices still get 255 bytes.
- The daemon no longer clips in clean(); it clips per device (2047 or 255 bytes, cut on a UTF-8 boundary with '…') and stores exactly what it sent, so the hold types what was shown.
- Recording cap 15 s -> 60 s (VOICE_MAX_MS); the daemon accepts up to 70 s; the transcript wait 20 s -> 45 s for the longer whisper run.
- The watch's existing paging (More n/N, hold locked until the last page) shows it.
- If texts ever outgrow one frame, reuse the detail offset-fetch (cmd_op_detail) rather than a new protocol.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 87b4bf2 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-12T23:55:30Z · copilot-cli/opus@mac-work · macos-arm64 · 87b4bf2: filed
- 2026-11-12T23:55:33Z · copilot-cli/opus@mac-work · macos-arm64 · 87b4bf2: claimed
- 2026-11-13T00:04:47Z · copilot-cli/opus@mac-work · macos-arm64 · 87b4bf2+dirty: Built: proto 0.16. Watch holds 2047 B transcripts in PSRAM (link_rx, voice.c, ui_voice.c), VOICE_MAX_MS 60 s, VOICE_WAIT_MS 45 s. Daemon: clean() no longer cuts; device.rs fits the text per device (2047 or 255 B) and stores what it sent. Sim: new voice_minute_transcript (60 s auto-stop, 1515 B in 9 pages, hold locked until page 9); sim_script line and inject buffers raised to 4 KB. Also fixed atlas messages.js missing fw_* (types 24-26) from b769. Checks: sim ctest 168/168, cargo fmt/clippy/test, idf build + check_dram (DIRAM free 183629 B), vectors unchanged, atlas extract --check + node 186/186.
- 2026-11-13T00:09:28Z · copilot-cli/opus@mac-work · macos-arm64 · 76a63d6: Pushed 5367d01; the watch runs it (OTA over USB, hello proto 0.16, clean boot). Daemon reinstalled. Waiting for the user's live test of a long dictation (over 15 s, transcript over 255 bytes) before closing.
- 2026-11-13T19:33:48Z · copilot-cli/opus@mac-work · macos-arm64 · ed9c889: fixed: user confirmed on the watch, 2026-11-13
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: ed9c889 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
