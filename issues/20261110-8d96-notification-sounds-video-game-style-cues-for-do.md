---
id: 20261110-8d96
title: Notification sounds: video-game style cues for done and select, auditioned in the atlas
status: claimed
severity: low
area: atlas
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-10T20:59:03Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Sound bank as data in firmware core (sound_bank.c), a chip synth spec both the watch and the atlas follow, and an atlas chapter to audition candidates per role (done, select; more later). Speaker playback on the board (ES8311 + amp) is separate.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 6eaa32d+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T20:59:03Z · copilot-cli/opus@mac-work · macos-arm64 · 6eaa32d+dirty: filed
- 2026-11-10T20:59:05Z · copilot-cli/opus@mac-work · macos-arm64 · 6eaa32d+dirty: claimed
- 2026-11-10T21:03:11Z · copilot-cli/opus@mac-work · macos-arm64 · 6eaa32d+dirty: Sound bank (sound_bank.c/h, 6 done + 6 select candidates), extract.py -> facts.sounds, atlas chapter 04 Sound with synth.js audition. Checks: extract --check, node tests 134/134, sim ctest, idf build (DRAM free 128777). Awaiting user's picks; board playback still TODO.
- 2026-11-10T21:10:38Z · copilot-cli/opus@mac-work · macos-arm64 · 24a605d+dirty: Doubled to 24 candidates. Synth gains SINE/FM/BELL waves, up to 3 layered tracks and feedback echo (contract in sound_bank.h). Checks: extract --check, node 135/135, sim ctest 68, idf build.
- 2026-11-10T21:17:12Z · copilot-cli/opus@mac-work · macos-arm64 · f21953c+dirty: Added 20 original homages (done + select for 10 classic games' styles); no game audio or melodies copied, Korobeiniki is public domain. 44 candidates. Checks: node 135/135, sim 68, idf build.
