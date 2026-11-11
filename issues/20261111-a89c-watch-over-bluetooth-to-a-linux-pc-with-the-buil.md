---
id: 20261111-a89c
title: Watch over Bluetooth to a Linux PC with the built-in BCM4350C0 (MacBook Pro 2016-17)
status: fixed
severity: med
area: firmware
platforms: linux,esp32
owner: claude-code@linux-dev
created: 2026-11-11T01:34:10Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

User request 2026-11-10: wireless between the watch and the Linux dev box, whose built-in Bluetooth
(BCM4350C0, hci_uart/hci_bcm) never made an LE connection. Two bugs, one per side:
1. Linux: the chip's ROM sets reserved bits in LE Advertising Report event types (0x20/0x23/0x24),
   the kernel drops every report ("unknown advertising packet type"), so BlueZ never sends LE Create
   Connection. Fix: patched hci_bcm (tools/linux/bcm4350c0-le-fix, DKMS).
2. Watch: for this central NimBLE reports BLE_GAP_EVENT_CONNECT with status 26 although the link
   works, so s_conn stayed NONE and every send failed silently. Fix: adopt the handle on subscribe,
   ignore a disconnect for a stale handle, log why a send fails.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 173bdd3+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T01:34:10Z · claude-code@linux-dev · linux-x86_64 · 173bdd3+dirty: filed
- 2026-11-11T01:34:25Z · claude-code@linux-dev · linux-x86_64 · 173bdd3+dirty: claimed
- 2026-11-11T01:34:25Z · claude-code@linux-dev · linux-x86_64 · 173bdd3+dirty: fixed: this commit; verified live on linux-x86_64 + the StopWatch: patched hci_uart loaded (insmod), GATT connect, daemon 'device connected ... (ble)' and the watch welcomed by this PC over BLE. ctest 71/71, idf build + check_dram ok. DKMS install (persistence across reboots) not run yet: needs sudo.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: 173bdd3+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
