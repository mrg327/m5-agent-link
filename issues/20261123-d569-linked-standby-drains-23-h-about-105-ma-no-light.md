---
id: 20261123-d569
title: Linked standby drains 23 %/h (about 105 mA): no light sleep, BLE stays at 30 ms
status: open
severity: high
area: firmware
platforms: esp32
owner:
created: 2026-11-23T05:58:16Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

Measured 2026-11-22 with the power report (`agentlink power`), StopWatch fw b980ef1: unplugged at
13:26 at 4230 mV, linked over BLE the whole time with the display off, then flat about 4 h 45 m later
at 3094 mV (58 reports, one segment). The host was awake and on AC throughout (keep-awake held), so
the link never dropped and the watch never reached DEEP.

- `off` (linked, display off): 23.4 ± 1.1 %/h, about 105 mA at 450 mAh. 100 % of the window.
- BLE interval 30 ms in every report (`itvl_us` 30000), never slower.
- FIRMWARE.md §4 budgets STANDBY at 1.5-3 mA (auto light sleep, about 1 s interval with peripheral
  latency, display rail off). Measured is 35-70x that: linked standby costs as much as being awake,
  so a watch left linked to an awake host lasts under 5 hours.

## Cause (read from the code, not yet measured per part)

- `CONFIG_PM_ENABLE` is not set in firmware/sdkconfig, so there is no auto light sleep and no
  DFS: the CPU idles at full clock. STANDBY as built is "display off", not the designed state.
- link_ble.cpp keeps the base interval (`BLE_IDLE_ITVL` 24 = 30 ms) with the display off; nothing
  asks for a slow interval or peripheral latency in standby.
- The core loop polls every 50 ms with the display off (POWER.md), which would also block light sleep.

## Plan

1. Enable PM (DFS + auto light sleep with NimBLE modem sleep), check the 32 kHz clock source BLE
   needs for light sleep, and make the 50 ms poll event-driven or slower when the display is off.
2. With the display off and nothing pending, ask for a slow interval with peripheral latency (for
   example 30 ms base with latency so the radio wakes about once a second); go back to the fast
   interval on attention or input. Keep Apple's parameter rules.
3. Re-measure with `agentlink power` over a few hours linked and display off; update FIRMWARE.md §4
   and POWER.md with r_off. Watch the approve latency: an attention must still arrive within ~1 s.

Related: 20261117-0d15 (DEEP drain) is separate; this run had no deep time so it says nothing there.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 5d21b95 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-23T05:58:16Z · claude-code@linux-dev · linux-x86_64 · 5d21b95: filed
- 2026-11-25T16:19:37Z · claude-code/cloud@cloud · linux-x86_64 · b461e37: claimed
- 2026-11-25T16:30:16Z · claude-code/cloud@cloud · linux-x86_64 · 34d250c: Commit 34d250c (plan steps 1-2, code only): PM + tickless light sleep at rest (no-light-sleep lock while the display is on; dark display flushes synchronously; no light sleep with USB Serial/JTAG attached), BT modem sleep on the main crystal kept powered (G15/G16, the 32 kHz crystal pins, carry I2S), buttons polled every 50 ms and touch ungated at rest, BLE rest interval 60-75 ms latency 12 timeout 5 s (Apple rules), back to the central's base on wake; Kconfig AGENTLINK_LIGHT_SLEEP / AGENTLINK_BLE_REST_INTERVAL. Checks: format structure docs lint atlas sim (246/246) firmware PASS, check_dram 182957 B free (-1.8 KB). NOT run on the watch. To verify: delete firmware/sdkconfig, build, flash; on battery, linked over BLE, display off for 2-3 h; agentlink power should show r_off in the low mA (vendor: 3.5 mA Wi-Fi light sleep) and itvl_us 60000-75000. Also check: an attention buzzes within ~1-2 s at rest; button A/B and a tap wake the display; the USB link and OTA still work; no BLE drops (supervision 5 s). If light sleep breaks something, turn AGENTLINK_LIGHT_SLEEP off and measure the BLE interval alone.
- 2026-11-25T16:30:16Z · claude-code/cloud@cloud · linux-x86_64 · 34d250c: reopened: Released: needs a run on the watch (steps above)
