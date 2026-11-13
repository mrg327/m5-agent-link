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
