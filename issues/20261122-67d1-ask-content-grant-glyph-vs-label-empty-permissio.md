---
id: 20261122-67d1
title: ASK content: grant glyph vs label, empty permissions, long commands, 33rd session's request invisible
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §5 C3-C5). Grouped:
- The 33rd session is dropped (pool_full) and its request never shows; A double skips it (content/s07_max). Evict an ended session, or keep requests visible.
- The grant mask alone picks ✓/✗: mask 0 shows "Allow" as ✗, mask 0xFF shows "No" as a ✓ hold-to-grant; mask bits past n_opts are not checked; "don't ask again" has the same ✓ as a one-time yes (a04_masks, a03_fouropts).
- One grant option plus approve/deny: no deny on the watch (a02_oneopt).
- Empty title, text and tool: only the discs, approve at full emphasis (a07_empty; ui_ask.c ~805).
- Long commands wrap at the first space ("cd" alone, then the path); rm -rf never shows (a05_longtext, j01_jobs).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
