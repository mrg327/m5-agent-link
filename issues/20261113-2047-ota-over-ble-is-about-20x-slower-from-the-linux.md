---
id: 20261113-2047
title: OTA over BLE is about 20x slower from the Linux host than from macOS
status: claimed
severity: med
area: daemon
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-13T03:12:11Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

Seen 2026-11-12 on the StopWatch over BLE to the Linux PC (BCM4350C0 on the patched hci_uart, issue a89c):
`agentlink ota` sent 584 KB of a 1.3 MB image in about 300 s (fw_ack `next` 583680 at 19:11:05), about 2 KB/s,
so a full update takes about 11 minutes. The same image size went over BLE from macOS in 34 s end to end
(issue b769), about 38 KB/s. The user wants OTA to be the normal way to update the watch, so this needs to be
much faster. Over USB the update takes about 13 s.

## What is known

- Core ota.c already calls hal_link_interactive(true) during a transfer (fast interval request, 15–30 ms).
- The daemon sends 2 KB fw_chunks, 4 in flight (ota.rs CHUNK/WINDOW), written without response in
  244 B fragments (MTU 247); the watch answers one fw_ack per chunk.
- 2M PHY is requested at connect (link_ble.cpp).

## Plan

1. Measure first: the connection interval and PHY BlueZ actually grants (btmon or the watch's
   "interval" log line over serial), the fragment rate, the fw_ack round trip, and the flash write/erase time
   per chunk on the watch. Same numbers on macOS for comparison.
2. Likely levers, in the order the measurements point to: get the fast interval granted on BlueZ
   (or ask from the central side), a larger window or bigger chunks, data length extension, fewer acks
   (cumulative ack every N chunks), erasing ahead of the write, and the hci_uart baud rate of the
   Linux controller if it caps throughput.
3. Report time per MB before and after on both hosts; keep resume, rollback and the signature check intact.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 95b2543 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-13T03:12:12Z · claude-code@linux-dev · linux-x86_64 · 95b2543: filed
- 2026-11-13T05:06:26Z · claude-code@linux-dev · linux-x86_64 · c6ab57b: claimed
- 2026-11-13T05:06:26Z · claude-code@linux-dev · linux-x86_64 · c6ab57b: Measured from the 19:11-19:23 OTA trace: 928 fw_acks, one per 2 KB chunk every ~1.1 s (881 of 928 gaps between 1.0 and 1.2 s), so the rate is set by a ~1 s wait, not by link bandwidth. Writes stayed at 244 B (no 20 B fallback). Work on branch ota-speed.
- 2026-11-13T06:08:20Z · claude-code@linux-dev · linux-x86_64 · 4762df9: Branch ota-speed (not pushed): c040548 (BLE link setup ran only on connect status 0, but this BCM4350C0 central refuses the feature read so every link came up as status 26 and never asked for a faster interval; now a bulk 7.5 ms request during OTA with fallback, 30-45 ms re-request after a fast reconnect, MTU 239 when DLE is refused) and a2822fa (freeze: nimble_host spun forever on a ring lock held by the preempted main task on core 0; task_wdt IDLE0, no panic; now spin 256 then sleep a tick). Full 1284 KB image over BLE: 706 s -> 100 s (1.8 -> 12.8 KB/s), 110 s with restart. The chip refuses DLE and 2M PHY, so the interval is the only lever left besides compression (~60 s, needs a protocol change). Checks: fmt, clippy -D warnings, cargo test, ota_e2e, sim ctest 181/181, idf build + check_dram 182853 B free, signature verified. Watch runs c040548 via BLE OTA, no hang in 2 OTAs with console open. Mac not re-tested.
