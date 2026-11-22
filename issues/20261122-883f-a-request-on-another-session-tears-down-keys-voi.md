---
id: 20261122-883f
title: A request on another session tears down KEYS, VOICE, READ, GRAPH, STATUS, LABS: typed text and recordings are lost
status: open
severity: high
area: firmware
platforms: any
owner:
created: 2026-11-22T19:20:12Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §3 E1). on_attention() opens ASK for a new request (or error) on another session unless the user is already in ASK; enter_mode() closes the open mode. In KEYS that sends kb_end discard (the typed text is erased), even mid A-hold-to-send; in VOICE the recording or ready transcript is discarded, even mid hold-to-send; READ loses its page. After answering, the user lands on HOME. Breaks UI-V3 §1.6 "Nothing is lost".
Repro: simulator/scripts/ux (branch ux-profile)/events/e01_keys_att.txt, e02_keys_hold_att.txt, e03_voice_att.txt, e04_voice_hold_att.txt, e27_error_yank.txt, e05/e07/e08/e09/e10.
Code: app.c on_attention (the "busy in ASK" exemption), ui.c enter_mode → ui_keys_close / ui_voice_close.
Should: while the user composes (KEYS, VOICE) or reads, glow/buzz/badge instead of taking over; at least keep the draft and return to it. Suspected: quiet and hush still let ASK take over on a lit display.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:12Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
