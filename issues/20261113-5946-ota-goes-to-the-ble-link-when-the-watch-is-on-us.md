---
id: 20261113-5946
title: OTA goes to the BLE link when the watch is on USB and BLE at once
status: claimed
severity: med
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T00:06:57Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Found while updating the watch for f97a. With USB and BLE both linked, the watch serves only USB (link.c drops frames from the other transport once welcomed on USB), but ota.rs keeps the device's latest hello, here BLE's, so fw_begin goes to BLE and agentlink ota fails with 'no answer from the watch for 10 s'. Fix: like the watch, prefer the USB link: a hello on another transport does not replace a live USB link.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 5367d01 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T00:06:58Z · copilot-cli/opus@mac-work · macos-arm64 · 5367d01: filed
- 2026-11-13T00:06:58Z · copilot-cli/opus@mac-work · macos-arm64 · 5367d01: claimed
