---
id: 20261122-9689
title: Clipped strings lose their ellipsis and can end in half a UTF-8 character; AGENT header clips numbers
status: open
severity: med
area: protocol
platforms: any
owner:
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §5 C2). The daemon's clip() counts characters and appends "…" (3 bytes); the device's buffers are bytes (opts 25, title 41, repo/branch 33, summary 81, hn 17) and model_copy_str cuts again without utf8_drop_partial. "Yes, don't ask again fo…" shows as "Yes, don't ask again fo", so a cut grant option reads as whole. Stray lead bytes reach the model, dbg_ui_state and the log (suspected: a strict UTF-8 decoder on the daemon could reject those messages). The AGENT header readout is a centred clip: "Claude Opus 4.5" shows as "e Opus 4.5", "PR #123456" as "12".
Repro: simulator/scripts/ux (branch ux-profile)/content/a03_fouropts.txt, a06_utf8cut.txt, s02_utf8_cut.txt, x01_meta.txt.
Code: daemon broker.rs / registry.rs clip; model.c model_copy_str; link_rx.c STR fields; ui_gauges.c:43.
Should: clip to the byte budget on the daemon, utf8_drop_partial on the device for every string, never clip a number.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
