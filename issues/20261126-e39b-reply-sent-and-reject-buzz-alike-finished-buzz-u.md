---
id: 20261126-e39b
title: Reply-sent and reject buzz alike; finished buzz under the 35 ms floor
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T04:56:44Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

feedback.c:111 reply-sent {35,60,35,60,35} vs :117 reject {35,70,35,70,35}: success and failure feel the same (UI-V2 §5: never by 10 ms). A held grant option goes out through reply (ui_ask.c:1173) and ends in the same ticks. feedback_finished uses 30 ms pulses (feedback.c:151), under the stated 35 ms floor. Found by the 2026-11-26 UX research (detail-1 §1).

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
