---
id: 20261126-74ee
title: Coin, die and oracle in Experiments
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T05:23:34Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

UX research 2026-11-26, owner picked it in the walkthrough; design brief in the research notes (detail-5 idea 17). Turning white disc, die ring with digits, held oracle glyph; blocked while a request is open. Oracle glyphs to be tried on people. Depends on the toy groundwork.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 064af3a claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T05:23:34Z · claude-code/cloud@cloud · linux-x86_64 · 064af3a: filed
- 2026-11-26T09:16:06Z · claude-code/cloud@cloud · linux-x86_64 · 4433ae1: claimed
- 2026-11-26T09:40:25Z · claude-code@cloud · linux-x86_64 · 7088415+dirty: coin, die and oracle as one LABS toy (Decide): decide.c, toy_rand.c, toy_motion.c, ui_decide.c; dice glyph in the 20-64 px icon fonts; FUN_TOYS wired in feedback.c and LABS; tests test_decide and sim decide_coin, decide_die, decide_oracle; the oracle glyphs are still to be tried on people
- 2026-11-26T09:46:51Z · claude-code/cloud@cloud · linux-x86_64 · 186b561: fixed: f16b4fe + Decide toy; oracle glyphs to be tried on people; all checks incl. ESP-IDF firmware build pass at 186b561; Atlas WASM rebuilt; not yet felt on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 186b561 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
