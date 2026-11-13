---
id: 20261113-82dc
title: Queue a reply or dictation for a working agent; the daemon types it when the session can take input
status: claimed
severity: med
area: protocol
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T00:16:48Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

While an agent works, the watch offers only Notify, Snooze and Stop. The user wants to queue a message
(quick reply or dictation) and have it sent the next time the agent can take input: "hold on to the message
until we are able to send it next time" (no expiry).

Design (proto 0.17, PROTOCOL.md §6d):
- cmd `q` (key 6, bool) with `type` / `dictate`: queue if the session can't take input now; `denied` with a rid.
- `cmd_op_unqueue` = 18: drop the queued message; `stale` if none.
- `session.qd` (key 35, tstr): the queued message's start (<= 48 chars), "" = none; "…" under privacy meta;
  never in the trace or audit log. Sent only to proto >= 0.17.
- Daemon: one message per session (a new one replaces it), delivered after ~2 s once the session leaves
  start/work with no open request and no approval/question, with the usual typing checks; kept on failure;
  dropped with the session; memory only (lost on daemon restart).
- Watch: "Queue reply" in the Working agent menu (global replies + Dictate), voice Send becomes Queue,
  "Cancel queued" and a "Queued: …" line when qd is set; hello minor 17.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: d1ad260+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T00:16:48Z · copilot-cli/opus@mac-work · macos-arm64 · d1ad260+dirty: filed
- 2026-11-13T00:16:53Z · copilot-cli/opus@mac-work · macos-arm64 · d1ad260+dirty: claimed
