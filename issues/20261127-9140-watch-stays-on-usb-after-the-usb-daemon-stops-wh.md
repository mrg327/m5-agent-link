---
id: 20261127-9140
title: Watch stays on USB after the USB daemon stops while the cable is plugged in; never falls back to BLE
status: open
severity: med
area: firmware
platforms: esp32
owner:
created: 2026-11-27T03:26:20Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## What happens

Seen 2026-11-26 on the StopWatch (fw 164c508, linux host). A hand-run USB-only bench daemon (BENCH.md §7)
welcomed the watch on USB while the BLE-only service held a BLE link. After the bench daemon was stopped
(port closed, cable still plugged in) the watch stayed welcomed on USB: the service's BLE link sent nothing
for 15+ minutes and `agentlink status` showed `devices: 0`. Only a reset brought the handshake back.

Cause, `firmware/components/core/src/link.c`: once `g_link.welcomed` with `kind == HAL_LINK_USB`, the only
way back to a handshake is `link_on_state(HAL_LINK_USB, false)`. `up[USB]` follows the cable / CDC state, not
whether a host reads the port, so closing the port on the host is not a down event. `link_tick()` keeps
sending the 60 s idle ping into USB; nothing checks that it is answered. BENCH.md §7 ("goes back to the
service's BLE link when the bench daemon stops") is therefore wrong today.

## What should happen

A welcomed link that goes silent drops its welcome and restarts the handshake on every transport that is up,
so BLE takes over within seconds. Options: treat an unanswered idle ping (no rx for, say, 2 x IDLE_PING_MS, or
a shorter ping while another transport is up) as link loss; and/or have the USB HAL report down when the host
stops reading (tx stalls / DTR drop on the USB-Serial-JTAG). The same timeout covers a BLE central that
stops answering without a disconnect.

## Checks

sim test: welcome on USB with BLE up, stop answering, expect a hello on BLE and a welcome there; on the watch,
the bench run beside the service, then stop the bench daemon and see the service list the watch again.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: cbee2c1 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-27T03:26:20Z · claude-code@linux-dev · linux-x86_64 · cbee2c1: filed
