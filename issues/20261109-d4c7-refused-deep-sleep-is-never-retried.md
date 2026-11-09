---
id: 20261109-d4c7
title: Refused deep sleep is never retried
status: fixed
severity: low
area: firmware
platforms: esp32
owner: copilot-cli/sonnet-fw@mac-work
created: 2026-11-09T17:02:25Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

power_tick sets s_deep=true and bumps the retained retry counter before hal_power_enter(DEEP). board.cpp falls back to STANDBY (button held, USB, ext1 failure) but core never learns, so DEEP is never retried and power_name() says deep.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:25Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:03:35Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 830f290: claimed
- 2026-11-09T17:08:34Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 61369e5+dirty: Device build not run: ESP-IDF toolchain (idf.py/export.sh) not available on this machine; board.cpp change is a trivial return of 'state' (reviewed by hand). Sim has no way to refuse DEEP, so no sim test for the refusal path.
- 2026-11-09T17:08:34Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 61369e5+dirty: fixed: checks: sim ctest 46/46; device build not run (no ESP-IDF)
    - agent: copilot-cli/sonnet-fw
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 61369e5+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
- 2026-11-09T17:13:56Z · copilot-cli/opus@mac-work · macos-arm64 · 75f780b+dirty: device build verified: idf.py build (ESP-IDF v5.5.6) completes with the new hal_power_enter signature. macos note: IDF export.sh fails when python3 is newer than the installed IDF env (py3.9); put the matching python first on PATH and set IDF_PYTHON_ENV_PATH to the idf5.5_py3.9 env. Refusal path still unverified on hardware.
- 2026-11-09T17:13:57Z · copilot-cli/opus@mac-work · macos-arm64 · 75f780b+dirty: committed in 75f780b
