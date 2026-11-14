---
id: 20261114-101b
title: BLE links to several machines at once (one watch, two or more daemons)
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-14T03:40:32Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

The watch takes one BLE central (CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1) and stops advertising once linked, so
with two daemons in range (the Linux laptop and the Mac) whichever connects first owns the watch and the
other sees nothing. Seen live 2026-11-13 while testing 203f: the Mac's daemon held the watch and the Linux
daemon could not reach it until the Mac's was stopped. The welcome already names the host; link.c picks
one live link (USB over BLE).

Wanted (user, 2026-11-13): the watch linked to several machines at once, sessions from all of them on the
ring, each answer, reply and focus routed back to the machine that owns the session.

Questions to settle before building: how many links (2-3; NimBLE max connections, controller memory and
internal DRAM, see check_dram), how sessions are keyed and shown per host (sids clash across daemons),
which daemon gets the watch-wide traffic (OTA, power reports, cfg, keyboard/voice, debug), whether
USB-preferred still means one link, the connection-interval budget for two links (BLE OTA wants 7.5 ms),
battery cost, and which link a new delta OTA base comes from.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 9d039a1 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-14T03:40:32Z · claude-code@linux-dev · linux-x86_64 · 9d039a1: filed
- 2026-11-14T05:11:28Z · claude-code@linux-dev · linux-x86_64 · b4beb9d: claimed
- 2026-11-14T05:38:43Z · claude-code@linux-dev · linux-x86_64 · 57b23ad: Design in docs/design/MULTI-LINK.md: 2 slots (any transports), one per host id; sids packed (slot<<28), rids mapped (daemon rids are random 32-bit); snapshot per slot; primary slot owns RTC, alert, power reports; OTA from any slot; no daemon change required. Spike on the watch (worktree, not committed): MAX_CONNECTIONS=2 + BLE_MAX_ACT=4 costs 1.1 KB internal heap (61,611 vs 62,715 B free after nimble_port_init; 57,075 vs 58,179 once linked); static DIRAM unchanged at 181,905 B; advertising kept on while linked works (seen from this laptop's scan with its daemon linked). Not yet tried: a second central actually connected (needs the Mac). Open questions marked [ask] in the doc. Watch back on main 57b23ad.
- 2026-11-14T20:04:04Z · claude-code@linux-dev · linux-x86_64 · ae90e41: User decisions 2026-11-14: two machines, 30 s grace, ring ticks; USB-third-host, usage gauges and allowlist still open. Phase 1 on branch multilink (a957daa): BLE HAL per central (HAL_LINK_BLE2 = port 3), advertising 100-150 ms with none / 500-750 ms with one / off with two, reassembly buffers in PSRAM, bulk/interactive per port; sdkconfig.defaults MAX_CONNECTIONS=2, BLE_MAX_ACT=4 (an old firmware/sdkconfig keeps 1: delete it). Checks: ctest 204/204 (new sim_link_two_ble: second central unheard until the first drops, then takes the link); check_dram 185,937 B free (+4 KB); on the watch: links to this laptop, delta OTA over BLE 31 s with the bulk request on the right handle (step_max_ms 112). Not yet: two real centrals (needs the Mac's daemon).
- 2026-11-14T20:51:05Z · claude-code@linux-dev · linux-x86_64 · 0f7e662+dirty: User decision 2026-11-14: machine-specific items (usage gauges, trackpad target, sessionless dictation) follow the machine last used, not BLE signal strength (radio/power cost, noise). Design MULTI-LINK.md §5.1: proto 0.23 used {idle} from the daemon's presence idle time, at once on input after 60 s idle then every 10 s while used; the watch keeps now - idle per slot by its own clock; never-used slots fall back to the primary. Clock, sound and power reports stay with the primary.
