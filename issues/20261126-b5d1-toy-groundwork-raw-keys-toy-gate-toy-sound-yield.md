---
id: 20261126-b5d1
title: Toy groundwork: raw keys, toy gate, toy sound, yield and return
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

UX research 2026-11-26, owner picked it in the walkthrough; design brief in the research notes (detail-5 groundwork). Raw-key mode in gesture.c (no 300 ms double-press wait) for toys; a toy feedback gate (toys never sound over alerts; mic stopped before an alert cue); a toy sound block in PSRAM; toys yield to a request at once and return to the paused toy after answering; the trackpad keeps today's waiting-dot behaviour; a timer obeys the alert mode, not timed Quiet; fix EXPERIMENTS.md accordingly.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 064af3a claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T05:23:34Z · claude-code/cloud@cloud · linux-x86_64 · 064af3a: filed
- 2026-11-26T05:23:34Z · claude-code/cloud@cloud · linux-x86_64 · 064af3a: claimed
- 2026-11-26T05:28:14Z · claude-code@cloud · linux-x86_64 · 8b7d02a: started: raw keys, toy gate, toy sound block, MODE_TOY yield and return
- 2026-11-26T09:02:15Z · claude-code@cloud · linux-x86_64 · 8b7d02a+dirty: raw keys (gesture_set_raw_keys), toy gate (toy_gate.c, feedback_toy_*), toy sound block (sound_toy_pcm, PSRAM), MODE_TOY host and registry (ui_toy.h), yield to a request and return paused (s_resume_toy); EXPERIMENTS.md Toys; tests test_toy_gate, test_feedback, test_gesture, sim_toy_yield
- 2026-11-26T09:16:06Z · claude-code/cloud@cloud · linux-x86_64 · 4433ae1: fixed: c0b9527, e229b06; checks format structure commits docs lint protocol atlas daemon sim firmware (ESP-IDF device build) pass at 6bee2b3; Atlas WASM rebuilt; not yet felt on a watch
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 4433ae1 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
