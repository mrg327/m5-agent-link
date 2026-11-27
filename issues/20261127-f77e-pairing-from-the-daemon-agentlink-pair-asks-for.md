---
id: 20261127-f77e
title: Pairing from the daemon: agentlink pair asks for the watch's passkey and bonds it, no OS Bluetooth tool
status: open
severity: med
area: daemon
platforms: any
owner:
created: 2026-11-27T03:23:14Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## What happens

Since 9842798 (issue 20261125-8efe) the watch serves a central only on an encrypted link with an
authenticated key, and shows a 6-digit passkey to pair. PROTOCOL.md §8 says "the PC's Bluetooth settings
ask for the passkey once per host", but the daemon does nothing to start or help that.

Seen 2026-11-26 on linux (BlueZ, fw 164c508, service daemon built before 338b054): after the flash the
daemon's BLE connect succeeded (`ble linked to AgentLink-…`, `bluetoothctl info`: Connected yes, Paired no)
yet `agentlink status` showed `devices: 0` with no error anywhere. The watch's boot log had
`link_ble: encryption (handle 1): status 1283, not authenticated`: BlueZ had no agent to enter the passkey,
so the Security Request failed silently. The fix was by hand: `bluetoothctl`, `agent KeyboardOnly`,
`default-agent`, `pair <addr>`, type the digits from the watch, `trust <addr>`. A user cannot be expected to
find that from "devices: 0".

## What should happen

- `agentlink pair` (DESIGN.md §PC interface already names it): scan, pick the AgentLink watch (or the only
  one), start pairing, prompt "enter the 6 digits on the watch", bond, add the uid to
  `trusted-watches.txt`, then report the link welcomed. Linux: register a BlueZ Agent1 (KeyboardOnly) over
  D-Bus for the pairing; macOS / Windows: trigger the OS pairing prompt (CoreBluetooth and WinRT drive
  their own passkey UI) and wait for the bond.
- The daemon notices the state: a linked BLE watch that never sends a hello within a few seconds, or a
  link the watch drops for missing encryption, shows in `agentlink status` as "not paired: run agentlink
  pair" (and a desktop notification), instead of silently counting zero devices.
- `agentlink devices forget` also removes the OS bond, so a re-pair starts clean.
- Optional, from DESIGN.md (b): USB-first pairing, a plugged-in watch and daemon set up the bond without
  a passkey.

## Checks

daemon tests for the status / hint path; a live pair on linux, macos and windows (platform-sensitive: BLE).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 164c508 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-27T03:23:14Z · claude-code@linux-dev · linux-x86_64 · 164c508: filed
