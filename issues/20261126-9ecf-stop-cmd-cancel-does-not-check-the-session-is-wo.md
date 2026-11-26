---
id: 20261126-9ecf
title: Stop (cmd cancel) does not check the session is working
status: claimed
severity: med
area: daemon
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T04:56:45Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

device.rs type_cmd: the Cancel branch checks only hold_ms, not the session state, so an Esc can land on an idle prompt or inside a dialog. Refuse (stale) unless the session is working; job cancel is separate (detail-3 §19).

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
