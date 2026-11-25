---
id: 20261117-0d15
title: Deep sleep drains about 2 %/h, around 100x the design budget
status: claimed
severity: high
area: firmware
platforms: esp32
owner: claude-code/cloud@cloud
created: 2026-11-17T19:19:44Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

Measured 2026-11-12/13 on the StopWatch (fw a389685) with the power report (issue 20261113-ee04): unplugged at
4206 mV, linked for about 4 minutes, then unlinked (the host suspended) and in deep sleep for 9 h 07 m of a
9 h 18 m window (awake about 12 minutes in all). Woken at 3980 mV: 100 % -> 75 % by the LiPo curve. That is
roughly 2 to 2.5 %/h, about 10 mA at 450 mAh. Some of the first drop is the cell settling after a charge, but
not 25 points. FIRMWARE.md §4 budgets under 0.1 mA in DEEP. An earlier coarse run (39 minutes asleep, about
1 %) pointed the same way.

## Suspects

Parts that stay powered in DEEP (board.cpp, "only the RTC domain, PM1, IOE1 and the sleeping panel/touch rail
stay up"): the panel/touch rail kept up for tap to wake, the CST820 not in its sleep mode, the BMI270 left
running since the motion work (issue 20261112-fe29), IOE1 outputs or PWM, the ES8311/amp rail, the PM1 LED,
pull-ups on the I2C bus, GPIO hold states.

## Plan

1. Measure: a USB power meter or a shunt on the cell if available; otherwise A/B builds that switch one
   suspect off before DEEP, each left asleep for a few hours and compared with `agentlink power` (deep time
   and mV survive the wake).
2. Fix the largest draws first; keep button wake, and decide with the user whether tap to wake is worth its
   cost.
3. Update FIRMWARE.md §4 with measured numbers. This also sets the cost of POWER_TIMER_WAKES and
   DEEP_AFTER_MS (open choice in ee04).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 99589b3 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-17T19:19:45Z · claude-code@linux-dev · linux-x86_64 · 99589b3: filed
- 2026-11-23T05:58:25Z · claude-code@linux-dev · linux-x86_64 · 5d21b95: 2026-11-22 run (fw b980ef1) stayed linked to an awake host: 0 min deep, so no new deep number. It did measure r_off = 23.4 %/h (~105 mA), filed as 20261123-d569; fixing that (PM/light sleep) may also move the deep figure. Next deep A/B needs the host suspended or the daemon stopped, and a charged watch.
- 2026-11-25T16:31:21Z · claude-code/cloud@cloud · linux-x86_64 · 4ea0a2a: claimed
