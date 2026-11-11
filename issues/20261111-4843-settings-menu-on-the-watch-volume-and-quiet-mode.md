---
id: 20261111-4843
title: Settings menu on the watch: volume and quiet mode
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-11T01:47:28Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User request 2026-11-10: a settings menu on the watch to adjust the volume and to enter a quiet mode.

What exists today (STATUS screen, swipe down on the deck, UI-V2 §4.5): a Quiet row cycling
off -> 30 min -> 1 h -> off (device-side `quiet_until`), a Sounds on/off row (alert mode full/quiet),
and information rows (link, battery, privacy, sessions). There is no volume control: the speaker
(ES8311 + AW8737A, playback added in f718) plays the sound bank at a fixed level.

To decide/do:
- Where it lives: grow STATUS into "Settings", or a separate screen reached from STATUS.
- Volume: a level the user can step (e.g. 0-5 or 0-100 % via the rim), applied to the codec's DAC
  volume / sample scaling, persisted across reboots (NVS) and ideally reported to the daemon so
  `cfg` can set it too; a short preview cue when it changes.
- Quiet mode: keep the timed quiet, consider "until I turn it off" and a quiet schedule; make sure it
  silences the speaker and the buzz consistently with the alert modes.
- Simulator scripts and UI-V2.md for the new rows.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: b1ecbce main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T01:47:28Z · claude-code@linux-dev · linux-x86_64 · b1ecbce: filed
