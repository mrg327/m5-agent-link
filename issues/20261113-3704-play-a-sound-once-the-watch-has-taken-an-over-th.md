---
id: 20261113-3704
title: Play a sound once the watch has taken an over-the-air update
status: claimed
severity: low
area: firmware
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-13T01:59:40Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

The user wants an audible sign that an OTA update landed: a distinct sound after the update, never on a normal
power on (or after a USB flash).

## Plan

- New sound-bank role `updated` (a short rising flourish, distinct from `approved`).
- Play it in core/ota.c when the new image is confirmed (first welcome after an OTA boot: hal_ota_unconfirmed()
  was true). A USB flash or a power on boots a confirmed image, so it never plays then.
- Follows the same gates as the confirmation tones (Sounds on, volume, not face down); also a toast
  "Updated to <fw>" so a muted watch still says it.
- Same branch as 20261113-da11 (response) so it ships in the same OTA.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 1c53c7c HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-13T01:59:40Z · claude-code@linux-dev · linux-x86_64 · 1c53c7c: filed
- 2026-11-13T01:59:40Z · claude-code@linux-dev · linux-x86_64 · 1c53c7c: claimed
