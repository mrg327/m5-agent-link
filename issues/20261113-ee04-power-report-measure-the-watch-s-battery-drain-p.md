---
id: 20261113-ee04
title: Power report: measure the watch's battery drain per power state
status: claimed
severity: med
area: protocol
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-13T05:59:50Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

The user runs the watch on battery while iterating and wants to see its power use. Today the only data is the
BLE Battery Level (linear 3.3-4.2 V to 0-100 %, read once per connection unless polled) and a `batt` message
on 5 % steps. On 2026-11-12 a once-a-minute BlueZ poll showed roughly 25 %/h awake and linked, about 1.5 %/h in
deep sleep (coarse, early readings inflated).

## Plan

- Watch: a `power` report every few minutes and on hello: raw and filtered battery mV, vin, charging, seconds
  in each power state since the last report (screen on, dimmed, standby, deep), and the BLE connection
  interval when known. Battery Level characteristic (0x2A19) notifies on change.
- Daemon: log each report to a CSV under the agentlink data directory; `agentlink power` prints the discharge
  per hour overall and per state, using a LiPo voltage curve instead of the linear map.
- Protocol: next free minor after 0.18 and the OTA-speed work (2047). Spec docs/design/POWER.md.
- Open question for the user: re-enable POWER_TIMER_WAKES or lengthen DEEP_AFTER_MS (core/power.c); the
  watch goes deep 10 min after losing the link and then needs a button press to reconnect.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 546f48c HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-13T05:59:51Z · claude-code@linux-dev · linux-x86_64 · 546f48c: filed
- 2026-11-13T05:59:51Z · claude-code@linux-dev · linux-x86_64 · 546f48c: claimed
- 2026-11-13T05:59:51Z · claude-code@linux-dev · linux-x86_64 · 546f48c: Waiting for 2047 (ota-speed) to finish before any firmware build or OTA; branch power will be based on it.
- 2026-11-13T06:58:09Z · claude-code@linux-dev · linux-x86_64 · f288ede: Built on local branch power (1a01d61, 373bb4c, b607d46, a389685; proto 0.19 power message, Battery Level notify, ~/.agentlink/power-reports.csv, agentlink power with a per-state least-squares fit; POWER_TIMER_WAKES/DEEP_AFTER_MS unchanged pending the user). Checks: fmt, clippy -D warnings, cargo test 370 passed, power_e2e 19/19, ota_e2e 16/16, sim ctest 187/187, atlas 208/208, idf build, check_dram 182789 B free, signature verified. Deployed: BLE OTA c040548 -> a389685 in 114 s incl. restart, ota_confirm + updated tone on the console, no task_wdt/panic; daemon/hook/CLI installed from the same commit; first report seq 0 arrived on the welcome (4218 mV, on USB, itvl 45 ms). Waiting for hours of on-battery data before closing.
