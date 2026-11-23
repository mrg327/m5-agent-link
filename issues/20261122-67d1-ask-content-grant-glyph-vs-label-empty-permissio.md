---
id: 20261122-67d1
title: ASK content: grant glyph vs label, empty permissions, long commands, 33rd session's request invisible
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code/content@linux-dev
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
- 2026-11-23T06:49:13Z · claude-code/content@linux-dev · linux-x86_64 · ab35617: claimed
- 2026-11-23T07:17:41Z · claude-code/content@linux-dev · linux-x86_64 · 06335bf+dirty: Grant-glyph rule (nav.c nav_opt_reads, nav.h): PROTOCOL.md says grant_mask bit i = opts[i] grants (approve-class, hold only). Bits past n_opts are ignored. An option grants when its bit says so OR its label starts with a grant word (yes/allow/approve/accept/always/ok/proceed): a mask can only add a hold, never take a grant's hold away (mask 0 'Allow' is now a tick, hold to grant, read lock). Label starting with a refusal (no/deny/reject/decline/cancel/never/don't/stop) under a set bit is 'unclear': warning glyph, locked like an unread grant, toast 'Unclear option: answer on your computer', commit() refuses it. A grant whose label says don't ask again / without asking / always / for this session gets the double tick (ICON_ALWAYS), hold unchanged. Questions keep their options as answers. Report lines carry (grants)/(denies)/(always)/(unclear).
- 2026-11-23T07:17:41Z · claude-code/content@linux-dev · linux-x86_64 · 06335bf+dirty: Deny: a permission with options none of which plainly denies (only grants or unclear) gets a Deny item after its options when acts has deny (one-option 'Yes' and mask 0xFF Yes/No). Blank permission (no title, text, tool): an eye struck through stands in the text block; grants are locked (toast 'Nothing to read: answer on your computer'); Deny and Open on PC still work. Long commands: a permission's text fills each line (fill_lines in ui_ask.c, break after the last space or '/' in the line's second half, else mid-word) instead of LVGL word wrap; a05 now reads 'cd /home/user/projects/' / 'customer-portal && rm -rf...'. Questions keep word wrap. 33rd session: model_upsert evicts a settled session (ended first, then idle or seen-done with the oldest state change, never one with a request, working, failed or an unseen finish) so a new session and its request show; if none qualifies it still drops (model.c, outside my group's files: noted for the string-clipping agent).
