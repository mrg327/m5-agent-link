---
id: 20261109-1173
title: Request ids restart at 1 on daemon restart (old rid could approve a new request)
status: fixed
severity: med
area: daemon
platforms: any
owner: claude-code@linux-dev
created: 2026-11-09T17:14:24Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: linux-x86_64
gh:
---

## Summary

Unconfirmed, from the review of 7d28268..48dafed. Broker::answer looks up by rid only and rids restart at 1 per daemon run. If the device keeps a pre-restart attention with rid N and the new daemon issues a different request with rid N, an approve on the old card would approve the new request. Check whether the device clears attentions on welcome/handshake; if not, seed rids randomly or per boot, or have answer() also match sid.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: aa1a69e main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:14:24Z · copilot-cli/opus@mac-work · macos-arm64 · aa1a69e: filed
- 2026-11-09T18:56:53Z · claude-code@linux-dev · linux-x86_64 · 609966f: claimed
- 2026-11-09T19:04:58Z · claude-code@linux-dev · linux-x86_64 · a27a357+dirty: Confirmed: daemon rids and sids both restart at 1; on_welcome kept every card and model_replace_all only drops cards whose sid vanished, so an old rid-N card survived and its answer (rid only) hit the new daemon's rid N. model_attention_add also carried answer_sent over to a re-sent rid, so a new request could inherit the old card's answer. Fixed both sides: daemon seeds next_rid randomly per run (below 2^31, wraps under u32::MAX); firmware drops all cards on welcome (the daemon re-sends open ones right after; the lag resend has no welcome, so 7dc6 still holds). Not flashed to the watch yet.
- 2026-11-09T19:04:58Z · claude-code@linux-dev · linux-x86_64 · a27a357+dirty: fixed: this commit; daemon fmt+clippy+test ok (new a_new_daemon_run_does_not_reuse_old_rids); ctest 48/48 incl. new sim_ask_cleared_on_new_link (fails without the fix); idf.py build ok; atlas extract --check ok. Verified on linux-x86_64 only, sim and device build; not flashed.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: a27a357+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
