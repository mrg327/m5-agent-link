---
id: 20261111-4843
title: Settings menu on the watch: volume and quiet mode
status: fixed
severity: med
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-11T01:47:28Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: macos-arm64
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
- 2026-11-13T18:07:54Z · copilot-cli/opus@mac-work · macos-arm64 · b9535d5: Mute model review (copilot-cli/opus, for the user to decide; nothing implemented). Today's silencers: Quiet (STATUS toggle 1, timed off/30 min/1 h: no buzz, sound, glow or nag on new alerts), Mute (STATUS toggle 2: alert FULL<->QUIET, i.e. sounds off, vibration kept), SILENT (daemon config only: no vibration either), volume 0-5 (prefs, NVS; no control on the watch), face down (silences everything), palm cover (stops current playback + that request's reminders), per-session snooze (agent menu), presence hush (daemon, SF_HUSH). Proposal B: toggle 1 'Alerts' bell/bell-slash cycling off -> 30 min -> 1 h -> until I turn it off; toggle 2 'Sound' speaker stepping Off/Low/Med/High over the existing volume pref (vibration always on); About unchanged; SILENT stays daemon-only. Concern: stepping volume makes muting take several taps, so an alternative is to keep a one-tap Mute and add volume as the fourth STATUS control UI-V3 4.6 reserves for this issue. Other options: A) keep the current model (icons now show state, c36c); C) B plus a separate Vibration toggle.
- 2026-11-13T18:09:11Z · copilot-cli/opus@mac-work · macos-arm64 · e74cdc8: claimed
- 2026-11-13T18:09:11Z · copilot-cli/opus@mac-work · macos-arm64 · e74cdc8: Scope (user 2026-11-13: 'We should also be able to adjust the volume to different levels'): keep the one-tap Mute and add a Volume control on STATUS stepping the existing prefs volume levels, with a preview cue. Quiet 'until off' left for later.
- 2026-11-13T18:17:56Z · copilot-cli/opus@mac-work · macos-arm64 · 3b81ba1: fixed: commit 3b81ba1: Volume toggle on STATUS (1-5, preview cue, unmutes). Checks: simulator ctest 187/187, idf.py build + check_dram (DIRAM free 182605 B), atlas --check + node 208/208. Quiet 'until off' and daemon-side volume split out: 20261113-c2fd  issues/20261113-c2fd-quiet-until-i-turn-it-off-and-volume-from-the-da.md. Not yet on the watch (Mac OTA key untrusted).
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 3b81ba1 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
