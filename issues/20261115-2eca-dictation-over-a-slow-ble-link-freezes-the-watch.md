---
id: 20261115-2eca
title: Dictation over a slow BLE link freezes the watch
status: claimed
severity: high
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-15T22:19:18Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Starting a dictation while linked by BLE to a Linux central left the watch unresponsive until it was reset. The daemon's trace showed audio blocks arriving at about 7 a second for over two minutes with no audio_end; the microphone makes about 34 a second, and a recording should stop at VOICE_MAX_MS (60 s).

Cause: voice.c pull() sent each block from inside its while-the-microphone-has-samples loop. A BLE send waits for the controller, and at this central's 48.75 ms interval the link carries under 2 kB/s against the microphone's 8 kB/s, so the loop never emptied the microphone and never returned: no UI, no buttons, no 60 s cap. It is in main, not in the glyph or multi-link work; the same freeze shows in the trace of 2026-11-14 with the old UI.

Fix: the microphone only fills a queue of encoded blocks (PSRAM, a minute's worth); a tick sends from it for 20 ms; a stop drains the queue and then sends the end; a recording asks the link for its short interval (hal_link_bulk). On a link that keeps up nothing changes: the end still goes in the stop's own tick.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: cace2b8 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-15T22:19:18Z · claude-code@linux-dev · linux-x86_64 · cace2b8: filed
- 2026-11-15T22:27:22Z · claude-code@linux-dev · linux-x86_64 · cace2b8: claimed
- 2026-11-15T22:27:39Z · claude-code@linux-dev · linux-x86_64 · d16f9c9: Fix in main with the multilink merge (060428d, d16f9c9) and on branch glyphs (6c20dae, which is what the watch runs). Simulator voice tests pass (8/8), full suite 204/204. Flashed; clean boot. Not yet confirmed by a dictation on the watch over BLE: check the daemon trace for the block rate, an audio_end and a transcript.
