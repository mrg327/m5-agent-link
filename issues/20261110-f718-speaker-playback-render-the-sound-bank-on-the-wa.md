---
id: 20261110-f718
title: Speaker playback: render the sound bank on the watch and play the picked cues
status: open
severity: low
area: firmware
platforms: any
owner:
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
