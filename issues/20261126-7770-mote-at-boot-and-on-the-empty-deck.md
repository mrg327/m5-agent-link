---
id: 20261126-7770
title: Mote at boot and on the empty deck
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-26T18:50:22Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Owner 2026-11-26: the Mote becomes the watch's mascot (design: research notes mascot/MASCOT.md). Guardrails kept: never talks, nags or proposes; never starts a buzz or sound; alerts win; warm white only; reduced motion; never the only sign of anything; absent from the lit deck with sessions and from ASK. Default off until a week on a device, then on. Boot: ember at centre, wakes, one flare, rises to 12 as the deck draws (1.2 s, silent, cut by any touch or request; no added boot time). Empty deck: rests at 12, wanders a little, ember after 5 min; still on battery.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: eec6958 claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T18:50:22Z · claude-code/cloud@cloud · linux-x86_64 · eec6958: filed
