---
id: 20261126-2404
title: Atlas review (Fable, 2026-11-26): text describes the old Ring UI; missing setup, keys, privacy; structure
status: claimed
severity: med
area: atlas
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-26T03:11:37Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Fable reviewed the whole Atlas at 87e3670 (report kept with the session; summary here). P1: Touch matrix
and lab and Glance cards describe the superseded Ring UI (tap = focus, swipe down = deny, centre disc of
words, Answer button) while the page's watch runs Sigil (nav.c); hero "One tap brings the right terminal
back" and Flow "tap the centre to send cmd focus" are wrong (key A focuses); "cannot drift" claim is false;
no problem statement, setup, pairing, keys legend or privacy/safety; one name per state. P2/P3: terms,
jargon, haptics from facts, chapter order (Flow after Touch, a Start chapter), Sound out of proportion,
mobile overlaps, favicon and CONTRACT.md stale.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 87e3670 claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T03:11:46Z · claude-code/cloud@cloud · linux-x86_64 · 87e3670: filed
- 2026-11-26T03:11:46Z · claude-code/cloud@cloud · linux-x86_64 · 87e3670: claimed
