---
id: 20261113-ef2c
title: Delta OTA: send a binary patch against the running image instead of the whole image
status: claimed
severity: med
area: protocol
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T20:14:40Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

## Summary

OTA sends the whole 1.31 MB image every time: ~34 s over BLE from macOS, ~100 s from the Linux BCM4350C0
(issue 2047), ~13 s over USB. Most updates change little, so send a binary delta against the image the
watch already runs, and rebuild the full image on the watch.

## Measurements (2026-11-13, signed images from real commits)

| change | full | zlib -9 | identical 4 KB blocks | bsdiff+heatshrink | bsdiff+lzma |
|---|---|---|---|---|---|
| small fix 95d85ba -> a109c12 | 1314816 | 790248 | 70/322 | 35411 (2.7%) | 15316 |
| one day 76a63d6 -> 95d85ba | 1314816 | 790246 | 12/322 | 135167 (10.3%) | 94840 |

Block-level "send only changed blocks" barely helps (code shifts move every block after a change); a
bsdiff-style delta handles shifts. Over BLE that is ~1 s (mac) / ~3-10 s (Linux) of transfer instead of
34 s / 100 s.

## Plan

- Protocol 0.21: `fw_begin` gains optional `psize` (patch length), `base` (SHA-256 of the base image) and
  `bsize` (base length). With a patch, `fw_chunk.off` / `fw_ack.next` count patch bytes; `size` / `sha`
  still describe the rebuilt image. The device hashes the first `bsize` bytes of its running slot and acks
  `bad` "Base mismatch" when they differ; the daemon then sends the full image.
- Patch format: detools sequential, bsdiff, heatshrink (the format Espressif's esp_delta_ota uses). The
  device vendors the detools C decoder (BSD-2, heatshrink ISC) in core, so the simulator tests it; it reads
  the base through a new HAL call and writes the rebuilt image through hal_ota_write as now. The SHA-256
  and signature check in esp_ota_end, rollback and in-session resume are unchanged.
- Daemon: generates the patch in Rust (no Python at run time); keeps the images it has sent and the
  images the firmware build produces in ~/.agentlink/fw/, picks the base whose version matches the
  watch's hello `fw`, and falls back to the full image when there is none, the patch is not smaller, or
  the watch refuses the base.
- Later: reproducible builds (no build date/time in the app description) to shrink deltas further.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: e8a5f5c+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T20:14:40Z · copilot-cli/opus@mac-work · macos-arm64 · e8a5f5c+dirty: filed
- 2026-11-13T20:14:45Z · copilot-cli/opus@mac-work · macos-arm64 · e8a5f5c+dirty: claimed
