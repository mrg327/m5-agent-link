---
id: 20261126-98a6
title: Living marks: shared motion grammar and fun switchboard
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

UX research 2026-11-26, owner picked it in the walkthrough; design brief in the research notes (detail-4 base + idea 1). One motion grammar for sigil, ring and hero glyph (breath via brightness and sway, dip-settle, shiver, lean-then-still, droop, radial joy; stillness = waiting for you); one 'fun' preference word with a bit per feature plus reduced motion; persona.c gate; temper.c second hash for per-sigil temperament. Guardrails: alerts always win, fun never starts a buzz or sound, continuous motion only on USB. Owner chose the full Living marks.

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
- 2026-11-26T09:03:05Z · claude-code/cloud@cloud · linux-x86_64 · e057b0f: Built in e057b0f: persona.c gate + fun word (LABS > Personality), temper.c temperament and poses, ui_living.c on HOME and ambient sigil + hero glyph. Ring left as is (its comet/sweep/flicker already follow the grammar; pips are another issue's). Checks: sim 264/264, atlas, structure, format, docs, lint, commits pass; firmware SKIP. Device: breath visibility at 72-100 %, frame time of the hero dip, USB-only loop.
- 2026-11-26T09:16:06Z · claude-code/cloud@cloud · linux-x86_64 · 4433ae1: fixed: d5c7bb4, fe7b2e0; checks format structure commits docs lint protocol atlas daemon sim firmware (ESP-IDF device build) pass at 6bee2b3; Atlas WASM rebuilt; not yet felt on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 4433ae1 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
