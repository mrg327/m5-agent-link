---
id: 20261126-59e0
title: The Mote: a light on the idle ring
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

UX research 2026-11-26, owner picked it in the walkthrough; design brief in the research notes (detail-4 idea 4). 8 px warm dot with a tail on its own track at r 216 of the idle face; laps as a seconds hand, settles at 6, ember, hovers at an error, cheers on a big finish; silent. Grows by days with green tests. Starts in LABS behind the fun switch. Depends on Living marks.

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
- 2026-11-26T09:46:50Z · claude-code/cloud@cloud · linux-x86_64 · 186b561: fixed: 7c801eb; hover at an error built but unseen (no idle face while an error is open), owner to decide; all checks incl. ESP-IDF firmware build pass at 186b561; Atlas WASM rebuilt; not yet felt on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 186b561 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
- 2026-11-26T17:11:08Z · claude-code/cloud@cloud · linux-x86_64 · d572e2f: Owner: keep the count badge; change the rule. The deck now goes ambient with an error open, so the Mote hovers beside it (7284098); checks incl. firmware pass.
