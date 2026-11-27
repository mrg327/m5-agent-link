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
- 2026-11-26T18:51:18Z · claude-code@cloud · linux-x86_64 · eec6958+dirty: Owner redesign: the Mote is a firefly (warm-white orb, breathing halo) on a track at r 175 inside the ring; it visits working sessions in turn, rests at 12, night is dimmer warm white (no amber); the level is counted but not shown; reusable painter ui_orb.c for the mascot
- 2026-11-26T19:39:58Z · claude-code/cloud@cloud · linux-x86_64 · 0ba1641: Firefly rework 65645b6 (owner chose Firefly, visiting busy sessions); halo fitted c2a4cb7.
- 2026-11-26T20:13:45Z · claude-code@linux-dev · linux-x86_64 · a276340+dirty: Frame budget check (BENCH.md §9): per-frame redraw in the sim (frames, expect_frame_px_max), px to watch ms by host CPU x 50 fitted on the UX-PROFILE bench tasks. Firefly on the idle face: one 4-5 k px area a frame, about 10 k at the loop and flare, predicted 8-16 ms, cadence met. Over: the boot move at size L (up to 39 k px, predicted 27-57 ms, about 20 fps for 0.6 s); near: the pairing flare (21 k px with the hidden Paired sign, 28 ms). No look-preserving fix found (banded rings: same pixels, slower); options for the owner in BENCH.md §9. sim_fun_temper's 4 M window was clock phase, not cost: the sim clock is now the same every run. Bench tasks bench/tasks/mote_*.txt await a watch run. Beside the Mote: issue 20261126-3385.
