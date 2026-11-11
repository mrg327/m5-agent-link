---
id: 20261111-f62b
title: Dictation over a slow BLE link freezes the watch (voice pull() never returns)
status: open
severity: high
area: firmware
platforms: esp32,linux
owner:
created: 2026-11-11T03:38:57Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Seen 2026-11-10 on the StopWatch (fw 173bdd3) over BLE to the Linux PC (BCM4350C0, issue a89c). After
starting dictation the dictation screen freezes. Releasing does nothing, VOICE_MAX_MS never stops it,
and the user has to power-cycle the watch (boot log: reset=poweron).

Trace (~/.agentlink/trace): aid 100 sent 89 blocks in 12.4 s, then the user power-cycled. aid 200
streamed 750+ blocks over 100+ s and was still going while the screen stayed frozen.
Both ran at ~7 blocks/s. 16 kHz ADPCM needs 16000/473 = ~34 blocks/s (~8 KB/s).

Cause, two parts:
1. voice.c pull() loops `while (hal_mic_read(...) > 0)` and sends one BLE frame per 473 samples.
   sw_ble_link_send() blocks on notify back-pressure (10 ms retries, up to 1 s per fragment). When the
   link is slower than the mic, the mic stream buffer never empties, so pull() never returns. The UI
   task stops rendering, takes no input and never reaches the VOICE_MAX_MS check.
2. Throughput: nothing asks for the fast connection interval during dictation (only scroll does,
   issue a6b7). This central's default interval gives ~1.7 KB/s. It was verified on macOS (fbdd2d3),
   where the link is faster.

Fix sketch: bound pull() per tick (time or block budget) so the UI and the stop/max checks always
run; request the fast interval during VOICE_REC; if the link still falls behind, fail clearly
("Link too slow") or drop audio. Do not wedge the UI.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: e479b43 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T03:38:57Z · claude-code@linux-dev · linux-x86_64 · e479b43: filed
- 2026-11-11T03:39:51Z · claude-code@linux-dev · linux-x86_64 · b8174a9: Not started: the user deferred the fix (2026-11-10). Workaround: dictate over USB, or hold power to restart the watch if it freezes. Related gap: the daemon logs the watch's non-'#' console lines (panics, ESP_LOG output such as 'notify back-pressure timeout') only at debug, so they don't reach the journal; run with RUST_LOG=info,device_log=debug when reproducing.
