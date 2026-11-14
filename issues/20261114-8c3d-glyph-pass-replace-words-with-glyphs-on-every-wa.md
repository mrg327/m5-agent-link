---
id: 20261114-8c3d
title: Glyph pass: replace words with glyphs on every watch screen
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-14T21:08:52Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User request (2026-11-14): go over all of the watch UI under the glyphs-over-words tenet (README Principles 4,
UI-V3.md §1.7). Show as much as is reasonable as images and glyphs that take a reasonable share of the screen,
instead of text throughout; trust the user to learn the symbols.

The core UI sources hold about 250 user-visible strings (ui*.c, app.c toasts, ota.c, gauges.c, ui_theme.c):
state words, action rows (Open on PC, Dismiss, Snooze 30 min, Reply, Stop ...), hold hints (Hold to approve ...),
toasts (Sent, Offline, Stopping ...), status lines (Quiet: off, Sounds: on, Volume 2 of 5), errors.

Kept as text whatever else changes: what the agent or the user wrote (request text, questions, options,
commands, repo and branch, transcripts, the standing rule), numbers, and the safety invariants of UI-V3 §1
(the approval card shows the request text).

Plan: an audit that classes every string (keep / glyph / number + glyph / drop), a glyph vocabulary shared by
all screens, then the screens in stages, each with before / after sheets for the user.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 6f1b753 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-14T21:08:53Z · claude-code@linux-dev · linux-x86_64 · 6f1b753: filed
- 2026-11-14T21:09:05Z · claude-code@linux-dev · linux-x86_64 · 6f1b753: claimed
- 2026-11-14T21:37:15Z · claude-code@linux-dev · linux-x86_64 · 54f022b: Stage 1 on branch glyphs: docs/design/GLYPHS.md (rules, vocabulary, audit of ~250 strings by screen, stages); theme_glyph() vocabulary, glyph legends and toasts on every screen, the home card converted (agent mark, 44 px state glyph + time, glyph buttons and chips). The UI report keeps the words, so no simulator script changed. Checks on linux-x86_64: ctest 203/203, atlas extract --check + 222 node tests, firmware build (image +65.5 KB for 39 new glyphs at 3 sizes, check_dram 181,601 B free). Before/after sheet sent to the user; ASK, AGENT, READ, WATCH, STATUS, VOICE, KEYS, SCROLL, TRACKPAD, LABS, OTA, GAUGES, DONE still to convert. Not flashed, not merged.
