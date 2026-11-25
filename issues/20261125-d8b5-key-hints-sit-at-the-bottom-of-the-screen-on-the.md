---
id: 20261125-d8b5
title: Key hints sit at the bottom of the screen; on the StopWatch the keys are on the upper rim (A yellow, B blue)
status: claimed
severity: med
area: firmware
platforms: esp32
owner: claude-code/cloud@cloud
created: 2026-11-25T18:58:52Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Found while fixing the Atlas (issue 20261125-d549). ui_kit.h places the key hints at the lower left (A,
amber) and lower right (B, "the physical button is red"). M5Stack's photos show, with the vendor's UI
upright and USB at the bottom: A yellow at about 10:30, B blue at about 1:30, the red button at 7:30 being
the PM1 power button. So either our rotation 0 is the other way up (USB at the top, keys at the bottom,
which puts B at the lower left and A at the lower right, still the reverse of ui_kit.h), or the hints sit
away from the keys. Needs the watch: which way up is rotation 0 (MOTION.md), then hint positions, the
orbit phases that avoid them (GLYPHS.md 222/50), the toast slot between them, and RGB_KEY_B (blue).

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 71c8cff claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-25T18:58:52Z · claude-code/cloud@cloud · linux-x86_64 · 71c8cff: filed
- 2026-11-25T23:13:49Z · claude-code/cloud@cloud · linux-x86_64 · 8f4f281: claimed
- 2026-11-25T23:18:58Z · claude-code/cloud@cloud · linux-x86_64 · b924959: Done here: RGB_KEY_B blue (key B is blue), GLYPHS.md updated, Atlas WASM rebuilt. Positions NOT changed. Tried moving the hint row to the top (UI_KEY_Y 101) and shot 37 sim screens: the AGENT/READ orbit arch (centred at 12, span 222) lands on the hints, and the ASK, approve, question and READ top text runs into them; mirroring the arch to 6 o'clock collides with AGENT's name, readout and last-message text. A real fix is a layout pass over those screens, worth doing only once rotation 0's physical orientation is known: with our firmware running, are the hints beside the physical keys or on the far side? If on the far side, the choice is (a) move hints and redesign the tops, or (b) default to ORIENT_FLIPPED and swap the hints' sides (keys at the bottom).
