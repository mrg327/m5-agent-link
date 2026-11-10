---
id: 20261110-f718
title: Speaker playback: render the sound bank on the watch and play the picked cues
status: claimed
severity: low
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-10T21:24:56Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Implement hal_sound_play on the StopWatch (ES8311 DAC + AW8737A amp: PYG3 rail, PYG10/SW_PIN_SPK_PA enable, I2S TX on PIN_DOUT 21). Render with the synth contract in core/src/sound_bank.h (match atlas/js/sound/synth.js), play snd_for("done") from feedback_finished and snd_for("select") on wheel commit; respect the STATUS sound toggle (ALERT_FULL). Picks: done-ff, select-ff.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 7a3a33a main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T21:24:56Z · copilot-cli/opus@mac-work · macos-arm64 · 7a3a33a: filed
- 2026-11-10T21:26:16Z · copilot-cli/opus@mac-work · macos-arm64 · b84fdea: claimed
- 2026-11-10T21:41:06Z · copilot-cli/opus@mac-work · macos-arm64 · b84fdea+dirty: Implemented: hal_audio_play/hal_audio_alloc replace hal_sound_play; core/src/sound.c renders the bank (C twin of synth.js, parity within 1-2 pulse-edge samples on 3/44 sounds) and plays done (feedback_finished, ATT_DONE) and select (feedback_tick) in ALERT_FULL; boards/stopwatch/audio.cpp shares codec+I2S0 full duplex (refcounted) with mic.cpp, speaker task with amp PYG10+G14, 2.5 s idle power-down. Sim 69/69 (new sound_render), atlas 135/135, firmware builds, DRAM free 128529 B. Flashed; awaiting listening test on the watch.
- 2026-11-10T21:50:07Z · copilot-cli/opus@mac-work · macos-arm64 · 3a89a03+dirty: Revised per user: role select renamed input; plays on ATT_APPROVAL/ATT_QUESTION (not on ticks), pick input-ff 3x with 90 ms gaps (snd_role_t repeat/gap_ms, honoured by atlas). Sim 69/69, atlas 136/136, DRAM free 128529 B. Flashed.
