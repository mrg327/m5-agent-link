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
- 2026-11-13T00:25:25Z · copilot-cli/opus@mac-work · macos-arm64 · e48296b+dirty: daemon: queue.rs (one delivery task on registry updates, settle 2 s, retry only on the session's next state change), set_qd, QUEUE_MINOR 17, qd redacted in trace, '…' under meta. watch: Queue reply / Cancel queued in agent menu, voice Queue mode, 'Queued: …' line, hello 0.17; sims queue_reply/queue_dictate/queue_show_cancel. Checks: cargo 329 pass; sim 171/171; idf build + check_dram free 183509 B; vectors unchanged; atlas 192/192.
- 2026-11-13T00:27:04Z · copilot-cli/opus@mac-work · macos-arm64 · 0fbec3f: On the watch: OTA to 0fbec3f, hello proto [0,17]; daemon reinstalled. Awaiting the user's live test before close.
- 2026-11-13T18:18:55Z · copilot-cli/opus@mac-work · macos-arm64 · c9fc524: User 2026-11-13: queued replies go stale waiting for the turn to end; send them immediately and accept the consequences. Change: q:true types at once while the agent works (start/work no longer hold it); the daemon holds it only while typing would answer something (an open request, or input with approval/question), delivering when that clears. Watch labels drop 'Queue'.
- 2026-11-13T18:23:30Z · copilot-cli/opus@mac-work · macos-arm64 · 3b4e6ce: commit 3b4e6ce: q types at once mid-turn; held only behind an approval/question/open request. Checks: cargo fmt/clippy/test (303 pass), simulator ctest 187/187, idf.py build + check_dram, atlas. Daemon reinstalled on mac-work (takes effect now with the old watch labels); new labels need a flash (Mac OTA key untrusted). Awaiting live test.
