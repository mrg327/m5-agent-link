---
id: 20261125-d549
title: Atlas watch is not faithful to the StopWatch: key positions and colours, power button, USB-C, Grove port
status: fixed
severity: low
area: atlas
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-25T18:55:16Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

Compared with M5Stack's product photos (docs.m5stack.com C152 main pictures, 2026-11-25): with the screen
upright (USB at the bottom, as the firmware draws it), key A is yellow at about 10 o'clock and key B is
blue at about 2 o'clock, both on the upper rim; the red button at about 7-8 o'clock is the power button;
USB-C is at the bottom (about 5-6 o'clock) with the HY2.0 port beside it at about 4 o'clock. The Atlas
draws A orange and B coral on the lower rim, USB-C on the right edge, and no power button or HY2.0 port;
the flow chapter's small watch has the keys at the sides. User asked for the keys facing up.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: cab8371 claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-25T18:55:16Z · claude-code/cloud@cloud · linux-x86_64 · cab8371: filed
- 2026-11-25T18:55:16Z · claude-code/cloud@cloud · linux-x86_64 · cab8371: claimed
- 2026-11-25T18:58:52Z · claude-code/cloud@cloud · linux-x86_64 · 71c8cff: fixed: commit 71c8cff; compared in headless Chromium with the vendor's main pictures 01, 02, 06, 07 (hero, glance and flow chapters); scripts/check format structure docs lint atlas PASS
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: 71c8cff claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
