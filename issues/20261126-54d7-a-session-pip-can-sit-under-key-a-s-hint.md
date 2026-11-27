---
id: 20261126-54d7
title: A session pip can sit under key A's hint
status: fixed
severity: low
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T04:56:45Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

With four sessions, when the fourth needs you its pip lands under the key A hint at the upper left (detail-1 §5). Move pips clear of the hint zones.

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
- 2026-11-26T05:23:02Z · claude-code/cloud@cloud · linux-x86_64 · 3f1556e: fixed: 1d15619; checks format commits docs lint atlas daemon sim pass (sim 256+); firmware device build not run (no ESP-IDF here); Atlas WASM rebuilt
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 3f1556e claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
