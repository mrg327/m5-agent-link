---
id: 20261122-4b25
title: A late result err after approving shows over another session's card; a failed approval reads as success
status: fixed
severity: high
area: firmware
platforms: any
owner: claude-code/takeover@linux-dev
created: 2026-11-22T19:20:12Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §3 E2). Approve, see Approved, back out and move to another session; a result err (stale/denied/timeout) then shows a 1.8 s glyph over the other session's card, and the request silently becomes answerable again without being reselected or reopened.
Repro: simulator/scripts/ux (branch ux-profile)/events/e14_late_err.txt.
Code: app.c on_result (toast only). Should: name the session the failure belongs to, select it, and bring the request back (or say plainly that it was not approved).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
- 2026-11-23T07:05:43Z · claude-code/takeover@linux-dev · linux-x86_64 · ab35617+dirty: claimed
- 2026-11-23T07:05:50Z · claude-code/takeover@linux-dev · linux-x86_64 · ab35617+dirty: on_result: a refused approve/deny/reply/dictate/answer brings its request back on its own session through ui_offer_attention (ASK, unless the user composes: then it waits as 883f), and the refusal glyph shows over it. If ASK is already open on it, ASK restores itself as before.
- 2026-11-23T07:25:16Z · claude-code/takeover@linux-dev · linux-x86_64 · 305e034: fixed: commit 405a679; checks: scripts/check format structure docs lint atlas sim firmware PASS (sim 236/236 with the virtual clock 4b308a0); regression test sim_takeover_late_err
    - agent: claude-code/takeover
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: 305e034 worktree-agent-a5e73b515ae79c7ff
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
