---
id: 20261112-fe29
title: Motion sensor (raise to wake, face-down silence, orientation), palm-cover mute, approve/deny tones
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-12T00:32:47Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

The board has a BMI270 that the firmware does not use. Branch motion.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 6d098e8 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-12T00:32:47Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: filed
- 2026-11-12T00:32:54Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: claimed
- 2026-11-12T02:41:33Z · claude-code@linux-dev · linux-x86_64 · c950d64: Built on branch motion (adf4ec9, d2981a8, d2df515), merged into next: BMI270 polled at 10 Hz, raise to wake, face-down silence, stored orientation (default normal), two-contact mute, approve/deny tones, volume 0-5. Device boot log: chip id 0x24, config upload ok, gravity about 1 g. Gestures unverified on the wrist; no settings rows yet.
- 2026-11-21T23:15:57Z · claude-code@linux-dev · linux-x86_64 · dc17059+dirty: Code merged and on the watch (main dc17059). Claim released; to close, live check: on the wrist: raise to wake, face-down silences, palm-cover mutes, approve/deny tones play.
