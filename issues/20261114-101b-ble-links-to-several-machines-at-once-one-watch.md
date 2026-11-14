---
id: 20261114-101b
title: BLE links to several machines at once (one watch, two or more daemons)
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-14T03:40:32Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

The watch takes one BLE central (CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1) and stops advertising once linked, so
with two daemons in range (the Linux laptop and the Mac) whichever connects first owns the watch and the
other sees nothing. Seen live 2026-11-13 while testing 203f: the Mac's daemon held the watch and the Linux
daemon could not reach it until the Mac's was stopped. The welcome already names the host; link.c picks
one live link (USB over BLE).

Wanted (user, 2026-11-13): the watch linked to several machines at once, sessions from all of them on the
ring, each answer, reply and focus routed back to the machine that owns the session.

Questions to settle before building: how many links (2-3; NimBLE max connections, controller memory and
internal DRAM, see check_dram), how sessions are keyed and shown per host (sids clash across daemons),
which daemon gets the watch-wide traffic (OTA, power reports, cfg, keyboard/voice, debug), whether
USB-preferred still means one link, the connection-interval budget for two links (BLE OTA wants 7.5 ms),
battery cost, and which link a new delta OTA base comes from.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 9d039a1 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-14T03:40:32Z · claude-code@linux-dev · linux-x86_64 · 9d039a1: filed
