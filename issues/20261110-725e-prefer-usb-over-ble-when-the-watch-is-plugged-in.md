---
id: 20261110-725e
title: Prefer USB over BLE when the watch is plugged in
status: fixed
severity: med
area: firmware
platforms: esp32
owner: claude-code@linux-dev
created: 2026-11-10T15:54:39Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

User request 2026-11-10: after a flash the watch was welcomed over BLE by another machine's daemon and
ignored the USB daemon on the PC it was plugged into. While welcomed elsewhere with a USB host
attached, offer a hello on USB (at once, then every 5 s, backing off to 30 s while sends time out);
a USB welcome takes the link over.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 8843f7d+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-10T15:54:39Z · claude-code@linux-dev · linux-x86_64 · 8843f7d+dirty: filed
- 2026-11-10T15:54:39Z · claude-code@linux-dev · linux-x86_64 · 8843f7d+dirty: claimed
- 2026-11-10T15:54:39Z · claude-code@linux-dev · linux-x86_64 · 8843f7d+dirty: fixed: this commit; test_link_arb updated (USB offer every 5 s, USB welcome takes over from BLE, BLE ignored after, fallback to BLE on unplug, back-off to 30 s when the USB send times out) and new sim_link_prefer_usb (BLE -> USB indicator switch); ctest 61/61; idf.py build ok. Verified on linux-x86_64 in the simulator; flashed for a live check.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: 8843f7d+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
