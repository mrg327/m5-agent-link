---
id: 20261126-34e5
title: Tap a key hint to press its key, with a count on A
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T05:23:33Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

UX research 2026-11-26, owner picked it in the walkthrough; design brief in the research notes (detail-1 §2). On HOME and WATCH a tap on a visible hint presses that key (ZONE_KEY_A/B, r 36). Taps act at once (no touch 'A twice'). White count badge on A equal to the needs-you pips; finished sessions not counted.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 064af3a claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T05:23:33Z · claude-code/cloud@cloud · linux-x86_64 · 064af3a: filed
- 2026-11-26T05:23:34Z · claude-code/cloud@cloud · linux-x86_64 · 064af3a: claimed
- 2026-11-26T09:16:05Z · claude-code/cloud@cloud · linux-x86_64 · 4433ae1: fixed: 490924d; checks format structure commits docs lint protocol atlas daemon sim firmware (ESP-IDF device build) pass at 6bee2b3; Atlas WASM rebuilt; not yet felt on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 4433ae1 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
