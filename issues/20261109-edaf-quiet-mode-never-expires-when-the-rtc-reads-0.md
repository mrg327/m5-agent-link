---
id: 20261109-edaf
title: Quiet mode never expires when the RTC reads 0
status: fixed
severity: med
area: firmware
platforms: esp32
owner: copilot-cli/sonnet-fw@mac-work
created: 2026-11-09T17:02:24Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

hal_rtc_get_unix() returns 0 when unset/VLF/I2C error. nav_quiet_next(…, 0) stores quiet_until=1800 and model_quiet_active(0) stays true, suppressing all alerts. Expected: an unknown clock never activates quiet; starting quiet with an unknown clock is refused.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:25Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:03:34Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 830f290: claimed
- 2026-11-09T17:08:34Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 61369e5+dirty: Sim: added 'rtc UNIX' script command; rtc 0 makes sim hal_rtc_get_unix() return 0 (unknown clock).
- 2026-11-09T17:08:34Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 61369e5+dirty: fixed: checks: sim_status_quiet_no_clock; ctest 46/46
    - agent: copilot-cli/sonnet-fw
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 61369e5+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
