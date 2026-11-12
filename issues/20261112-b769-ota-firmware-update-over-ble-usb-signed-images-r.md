---
id: 20261112-b769
title: OTA firmware update over BLE/USB (signed images, rollback)
status: fixed
severity: med
area: protocol
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-12T23:07:52Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

Firmware updates over the link (BLE or USB) instead of idf.py over USB (FIRMWARE.md §9). Protocol 0.15: fw_begin / fw_chunk (daemon → device, ≤ 2 KB, windowed) and fw_ack (device → daemon: next offset, status). The device writes the spare OTA slot sequentially; ESP-IDF 'Require signed app images' (RSA-3072, no secure boot, no eFuses) verifies the image against the key of the running app, so a BLE peer without the build key cannot flash the watch. Rollback: a new image confirms itself once welcomed by a daemon; a crash or no welcome within 5 min reboots into the old one. CLI: agentlink ota <image.bin>, progress over hook.sock. Watch shows an Updating screen (B held cancels).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 4458c6b main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-12T23:07:52Z · copilot-cli/opus@mac-work · macos-arm64 · 4458c6b: filed
- 2026-11-12T23:08:12Z · copilot-cli/opus@mac-work · macos-arm64 · 4458c6b: claimed
- 2026-11-12T23:10:07Z · copilot-cli/opus@mac-work · macos-arm64 · 4458c6b+dirty: Protocol 0.15 fw_begin/fw_chunk/fw_ack: CDDL, vectors, zcbor regen, Rust types, hal_ota_* contract, PROTOCOL.md §6c
- 2026-11-12T23:24:25Z · copilot-cli/opus@mac-work · macos-arm64 · ab840fc+dirty: Implemented: board ota.cpp (esp_ota_*, header check), RSA-3072 signed images without secure boot (key ~/.agentlink/fw_signing_key.pem made by the build), core ota.c + Updating overlay + rollback confirm, sim HAL + 14 tests, daemon sender (4-chunk window, rewind, resume) + agentlink ota CLI + ota_e2e.py. Checks: sim 167/167, idf build + check_dram ok, cargo fmt/clippy/test ok, ota_e2e 16 PASS. Flashed signed image over USB, boots.
- 2026-11-12T23:26:51Z · copilot-cli/opus@mac-work · macos-arm64 · dfb58a4: Live over USB: OTA ab840fc-dirty -> dfb58a4 in ~11 s, re-hello with new fw, confirmed on welcome. Unsigned and foreign-key images refused live ('Signature not trusted'), watch kept running.
- 2026-11-12T23:28:37Z · copilot-cli/opus@mac-work · macos-arm64 · dfb58a4: Live over BLE (USB transport off): dfb58a4 -> dfb58a4-dirty, 1.28 MB, 34 s end to end incl. restart and re-hello; CLI exit 0.
- 2026-11-12T23:28:37Z · copilot-cli/opus@mac-work · macos-arm64 · dfb58a4: fixed: commits ab840fc, dfb58a4; checks: sim ctest 167/167, idf build + check_dram (DIRAM free 182861 B), cargo fmt/clippy/test, ota_e2e.py 16 PASS; live USB + BLE OTA, unsigned and foreign-key images refused on the watch
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: dfb58a4 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74
