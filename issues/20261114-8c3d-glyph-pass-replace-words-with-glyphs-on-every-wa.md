---
id: 20261114-8c3d
title: Glyph pass: replace words with glyphs on every watch screen
status: fixed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-14T21:08:52Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
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
- 2026-11-14T22:37:03Z · claude-code@linux-dev · linux-x86_64 · 73ad4bb: User feedback on stage 1 (word-for-glyph swap in the old layout): looks identical; the UI must be strikingly different, borderline alien, text only for dictation, text entry and summaries. Three mock directions sent; user picked B Sigil (generated project marks instead of names, one giant state glyph, time as dots, actions as discs, menus as orbits). docs/design/GLYPHS.md rewritten as the Sigil design system (branch glyphs, f7af63d). User asked for sub-agents to rebuild each screen from the ground up: workflow running (foundation + HOME reference, five screen groups in their own worktrees glyphs-ask / -agent / -watch / -status / -voice, integration, read-only audit). Nothing merged to main or flashed.
- 2026-11-15T20:46:51Z · claude-code@linux-dev · linux-x86_64 · e675e16: Sigil rebuild integrated on branch glyphs (59a558d): HOME, ASK, AGENT, READ, WATCH, gauges, done row, STATUS, LABS, TRACKPAD, update overlay, VOICE, KEYS, SCROLL rebuilt from ui_kit primitives. The workflow lost its voice, integration and audit agents to a usage limit; the voice work was complete in its worktree and was committed after its tests passed, and the five branches were merged by hand (conflicts: icon lists, ui.c hit tests, toast slot modes, kit and vocabulary appends). Checks on the integrated tree: ctest 214/214; no assertion removed or loosened versus main (only moved tap/hold coordinates); firmware build clean, image 0x1a1000, check_dram 181537 B free; atlas facts, 222 atlas tests, wasm rebuilt. Not yet flashed.
- 2026-11-15T21:56:10Z · claude-code@linux-dev · linux-x86_64 · 2f93fce: Flashed glyphs 59a558d to the watch over USB: one clean boot, no asserts or panics in 14 s of boot log, daemon relinked over BLE.
- 2026-11-15T23:25:38Z · claude-code@linux-dev · linux-x86_64 · 1c6865c: Main merged into glyphs (aed13f8, 630e82e): Sigil UI + two BLE centrals + the dictation queue. ctest 215/215, firmware builds from a fresh sdkconfig (image 0x1a1000, 2 connections), check_dram 185553 B free, wasm rebuilt, 222 atlas tests. The watch runs glyphs + the dictation fix (6c20dae, one BLE connection); the combined build is not flashed.
- 2026-11-16T22:05:10Z · copilot-cli/opus@mac-work · macos-arm64 · 0253ee0: main 0253ee0 adds AGENT_PI = 4 (proto 0.23, pi coding agent, issue 20261116-f705): model.h, ui_theme.c theme_agent_name, model.c agent_name. The Sigil UI on glyphs needs a mark for pi when it merges.
- 2026-11-21T23:15:46Z · claude-code@linux-dev · linux-x86_64 · dc17059+dirty: fixed: Sigil UI merged into main (dc17059) with a pi mark (fe424a4). main dc17059 (glyphs merged), flashed over USB 2026-11-21, clean boot log, BLE relinked; ctest 215/215, check_dram 185553 B free, atlas 224/224, cargo test 413 pass (one flaky first run, clean on rerun)
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: dc17059+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
