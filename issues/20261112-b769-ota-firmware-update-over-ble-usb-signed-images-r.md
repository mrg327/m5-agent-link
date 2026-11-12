---
id: 20261112-b769
title: OTA firmware update over BLE/USB (signed images, rollback)
status: claimed
severity: med
area: protocol
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-12T23:07:52Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
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
