---
id: 20261125-8efe
title: BLE: pair with a passkey on the watch, encrypt the link, serve only trusted watches
status: open
severity: high
area: protocol
platforms: esp32
owner:
created: 2026-11-25T17:47:44Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

PROTOCOL.md §1 said: no bonding and no encryption (sm_bonding = 0, no-IO capability). Anyone in radio
range could read the traffic, and the daemon connected to any device advertising an AgentLink- name and
acted on its approve / deny / reply commands: a stranger's device could approve tool calls. User decision
2026-11-25: option A first (link-layer LE Secure Connections with a passkey on the watch, bonded; the
daemon serves only watches it trusts); the app-layer keys of PROTOCOL.md §8 later.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: ed9e935+dirty claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-25T17:47:44Z · claude-code/cloud@cloud · linux-x86_64 · ed9e935+dirty: filed
- 2026-11-25T17:47:44Z · claude-code/cloud@cloud · linux-x86_64 · ed9e935+dirty: claimed
- 2026-11-25T17:56:20Z · claude-code/cloud@cloud · linux-x86_64 · 338b054: Commits 9842798 (firmware) + 338b054 (daemon), option A: LE SC passkey pairing (display only, MITM, bonds in NVS, Security Request on connect, link up only when encrypted + authenticated, rx writes need ENC+AUTHEN; Kconfig AGENTLINK_BLE_SECURE), passkey overlay via hal_link_set_pair_callback (digits never logged), daemon trusted-watches.txt (USB adds, first BLE watch adds while empty, others refused: no welcome/snapshot; agentlink devices list|trust|forget). Checks: format structure docs lint protocol atlas daemon (435 Rust tests) sim (249, incl. sim_ble_pair) firmware all PASS; trust_e2e.py 8/8; summary/last_message/standing_rules e2e unchanged. NOT tried with a real central. To verify on the watch (fresh sdkconfig, flash): 1) the PC that already used the watch over BLE: forget the old OS pairing, reconnect; the OS asks for the passkey shown on the watch; after entry the watch shows the Paired toast and the daemon links. 2) Reboot the watch and the PC: reconnects with no prompt (bond). 3) Linux without a desktop agent: bluetoothctl, agent on, pair <addr>. 4) Windows: if no prompt, pair in Settings > Bluetooth. 5) A second PC: pairs with its own passkey (two bonds). 6) agentlink status on the first daemon lists the watch; a second watch with another uid over BLE is refused and listed trusted:false. Known limit (PROTOCOL.md §8): a device that copies a trusted uid and address is not caught until the app-layer keys.
- 2026-11-25T17:56:20Z · claude-code/cloud@cloud · linux-x86_64 · 338b054: reopened: Released: needs pairing tried on the watch with macOS, Linux and Windows (steps above)
