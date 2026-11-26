---
id: 20261126-38fa
title: Pip shapes per request kind, on the ring and the waiting dot
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

UX research 2026-11-26, owner picked it in the walkthrough; design brief in the research notes (detail-1 §5). Dot = question, bar = permission, triangle = error (shapes differ in outline, not a 10 px square). Owner: also the rim dot that says a request waits behind the screen; not on the selected session.

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
- 2026-11-26T09:16:05Z · claude-code/cloud@cloud · linux-x86_64 · 4433ae1: fixed: bd47342; the waiting mark became the count badge of 0a74 (6bee2b3); checks format structure commits docs lint protocol atlas daemon sim firmware (ESP-IDF device build) pass at 6bee2b3; Atlas WASM rebuilt; not yet felt on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 4433ae1 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
