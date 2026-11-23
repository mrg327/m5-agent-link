---
id: 20261122-9689
title: Clipped strings lose their ellipsis and can end in half a UTF-8 character; AGENT header clips numbers
status: fixed
severity: med
area: protocol
platforms: any
owner: claude-code/strings@linux-dev
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
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
- 2026-11-23T06:49:17Z · claude-code/strings@linux-dev · linux-x86_64 · ab35617: claimed
- 2026-11-23T07:16:43Z · claude-code/strings@linux-dev · linux-x86_64 · 8075751+dirty: Byte limits now live in one table, agentlink-proto limits.rs; the CDDL states the same numbers in bytes, and a daemon test (watchtext::the_watch_holds_every_limit) checks that model.h, model.c and link_rx.c buffers hold each one. On the daemon, Conn::send_now runs watchtext::for_watch on every message: drawable, then cut to the byte limit on a character boundary, ending in an ellipsis. broker fits title/text/opts at the source so attention.len stays right. The registry keeps its character clips for storage (CLI status); the fit at the device boundary is what decides. On the device, model_copy_str drops a partial character, ends a cut copy in an ellipsis (for older daemons) and swaps U+FFFD for the eye-slash glyph; ui_report_add drops a partial too (dbg_ui_state). link_rx's own utf8_drop_partial calls are gone. AGENT readout: the model name gets LVGL dots and a width that leaves room for the mode glyph and the whole PR number.
- 2026-11-23T07:30:25Z · claude-code/strings@linux-dev · linux-x86_64 · 4d11f9d: fixed: commits 313d45a, 4d11f9d, 2ad90e6; checks: scripts/check format structure docs lint sim atlas protocol firmware PASS; cargo test -p agentlinkd 347 pass (smoke tests fail only on SUN_LEN: the worktree path is too long for the Unix socket); regression tests sim_strings_clip, sim_sigil_gauges, watchtext::* and broker text_is_tidied_and_clipped
    - agent: claude-code/strings
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: 4d11f9d worktree-agent-a5f6ff572d25702e8
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
