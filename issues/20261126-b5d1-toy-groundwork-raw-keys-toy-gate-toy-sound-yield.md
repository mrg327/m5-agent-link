---
id: 20261126-b5d1
title: Toy groundwork: raw keys, toy gate, toy sound, yield and return
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T05:23:34Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
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
