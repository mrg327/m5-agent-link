---
id: 20261112-2777
title: Usage and context gauges on the watch (plan limits, context fill, model, mode, PR)
status: claimed
severity: med
area: protocol
platforms: any
owner: claude-code@linux-dev
created: 2026-11-12T00:32:54Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

From the market research: the most common feature in rival devices. Daemon reads Claude Code status-line data; new wire fields; arcs on the status screen, context capsule on the session card. Touches protocol, daemon and firmware. Branch next.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 6d098e8 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-12T00:32:54Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: filed
- 2026-11-12T00:32:54Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: claimed
- 2026-11-12T02:41:33Z · claude-code@linux-dev · linux-x86_64 · c950d64: Built on branch next (113d8d7, 499130a, 254ae1f): protocol 0.10 (usage message, session keys ctx/mdl/pm/pr/prs), agentlink-hook statusline shim with chaining, STATUS arcs, idle-face figure, context capsule, AGENT header readout. ctest 104/104 at that point, gauges_e2e 20/20. No CI check state: the status-line data has none. Not verified on a live subscriber session.
