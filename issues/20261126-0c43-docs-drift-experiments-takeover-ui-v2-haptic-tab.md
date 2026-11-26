---
id: 20261126-0c43
title: Docs drift: EXPERIMENTS takeover, UI-V2 haptic table, Copilot permission hook
status: fixed
severity: low
area: docs
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T04:56:45Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

EXPERIMENTS.md says a request takes over LABS, but ui.c mode_keeps_work() keeps LABS and TRACKPAD (request waits as a dot). UI-V2 §5's haptic table differs from feedback.c for question and done. STANDING-RULES.md §7 says Copilot CLI has no permission hook; GitHub now documents permissionRequest (approve/deny/interrupt) — record it as unverified on a real install and open follow-up. (UX research 2026-11-26.)

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 6eaa59d claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T04:56:45Z · claude-code/cloud@cloud · linux-x86_64 · 6eaa59d: filed
- 2026-11-26T04:56:45Z · claude-code/cloud@cloud · linux-x86_64 · 6eaa59d: claimed
- 2026-11-26T05:23:02Z · claude-code/cloud@cloud · linux-x86_64 · 3f1556e: fixed: 3587ffc, UI-V2 table in 245ee2d; checks format commits docs lint atlas daemon sim pass (sim 256+); firmware device build not run (no ESP-IDF here); Atlas WASM rebuilt
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 3f1556e claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
