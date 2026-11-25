---
id: 20261125-5df4
title: Desk mode: on USB the display stays on longer; document desk and wrist use
status: claimed
severity: low
area: firmware
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-25T16:48:53Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

The StopWatch ships without a strap and has a magnetic back; on a desk it is powered and linked over
USB, where battery does not matter. Today the display goes off 60 s after the last input even on USB,
so a desk glance goes dark. User decision 2026-11-25: on USB the display stays on longer, but not
always on (AMOLED burn-in). README should say how to use it on a desk and on the wrist.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: c581519+dirty claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-25T16:48:53Z · claude-code/cloud@cloud · linux-x86_64 · c581519+dirty: filed
- 2026-11-25T16:49:00Z · claude-code/cloud@cloud · linux-x86_64 · c581519+dirty: claimed
