---
id: 20261122-f724
title: power_tick idle check underflows when activity is stamped later than the step's clock
status: fixed
severity: high
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-22T09:16:07Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

core_step() reads app_now_ms() once and passes it to power_tick(). power_tick() compares
`now_ms - s_last_activity >= s_idle_ms` (and the face-down rule) unsigned. Anything that calls power_activity()
earlier in the same step with a fresh app_now_ms() makes s_last_activity later than now_ms, the difference wraps,
and the display turns off at once. Found by the bench (issue 20261122-bb2e), which now passes the step's own time;
today's callers pass now or run after power_tick, so nothing else triggers it yet. A signed difference
((int32_t)(now_ms - s_last_activity)) in power.c lines ~140-146 would make it safe for future callers.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: d9213f1 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T09:16:07Z · claude-code@linux-dev · linux-x86_64 · d9213f1: filed
- 2026-11-22T19:04:59Z · claude-code@linux-dev · linux-x86_64 · 14586dd: claimed
- 2026-11-22T19:04:59Z · claude-code@linux-dev · linux-x86_64 · 14586dd: Not latent: on_attention() calls power_attention(rgb, app_now_ms()) from drain_link, before power_tick(now). On the watch every request that arrived on a lit display turned it off 5-20 ms after ASK opened (8 of 8 in the bench log); the next touch or button was spent waking it. The simulator never shows it (the drain takes under 1 ms). Severity raised to high. Fix on branch ux-profile: signed elapsed checks in power_tick, unit test simulator/tests/test_power.c (fails before, passes after).
- 2026-11-22T20:39:33Z · claude-code@linux-dev · linux-x86_64 · c1a491c: fixed: merged into main (c1a491c); signed elapsed checks in power.c, test_power fails before and passes after; flashed b980ef1 and the bench's 11 tasks pass on the watch (8 of 8 requests had turned the display off before)
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: c1a491c main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
