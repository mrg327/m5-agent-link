---
id: 20261126-9fe3
title: Mote rides the update arc and the replay hour hand
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T18:50:23Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

Owner 2026-11-26: the Mote becomes the watch's mascot (design: research notes mascot/MASCOT.md). Guardrails kept: never talks, nags or proposes; never starts a buzz or sound; alerts win; warm white only; reduced motion; never the only sign of anything; absent from the lit deck with sessions and from ASK. Default off until a week on a device, then on. Firmware update: the orb rides the head of the progress arc. While you were away: the orb rides the replay's hour hand instead of a plain arc tip.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: eec6958 claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T18:50:23Z · claude-code/cloud@cloud · linux-x86_64 · eec6958: filed
- 2026-11-26T18:50:23Z · claude-code/cloud@cloud · linux-x86_64 · eec6958: claimed
- 2026-11-26T19:18:04Z · claude-code@cloud · linux-x86_64 · abcee00+dirty: Mote rides the update arc (ui_ota.c) and the replay hand (ui_replay.c) via new ui_mote_ride.c; FUN_MOTE off verified frame- and redraw-identical to abcee00 (14 shots, 4 px budgets)
- 2026-11-26T19:39:57Z · claude-code/cloud@cloud · linux-x86_64 · 0ba1641: fixed: 96c3196; failure frame left absent (overlay closes at once); all checks incl. ESP-IDF firmware build pass at 0ba1641; Atlas WASM rebuilt; frame-rate check follows; not yet on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 0ba1641 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
