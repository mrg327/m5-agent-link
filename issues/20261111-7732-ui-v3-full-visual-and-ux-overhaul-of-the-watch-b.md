---
id: 20261111-7732
title: UI v3: full visual and UX overhaul of the watch (branch ui-v3)
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-11T05:14:58Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User request 2026-11-10: the idea is right but the UI and UX lack polish. Audit every screen, then
rework the look and the interaction details to the standard of a first-party consumer wearable:
simple, intuitive, useful, with no loss of function.

Scope (branch `ui-v3`, worktree beside the main checkout, nothing merged until the user has reviewed
before/after sheets):
- Audit: docs/design/UI-PROFILE.md (every screen rendered in the simulator, findings by severity).
- Spec: docs/design/UI-V3.md (type, colour, icon and motion tokens; each screen; gesture changes).
- Presentation layer: ui_theme, ui_ring*, ui_wheel, ui_home*, ui_agent, ui_ask, ui_watch, ui_scroll,
  ui_status, ui_voice, ui.c (toast, transitions); generated fonts and icons.
- Interaction layer: nav.c only for one universal "back" and direct row taps. Every safety invariant
  of UI-V2 §1 stays (tap never approves, hold bound to rid, 300 ms arm, strict deny swipe, first
  touch after glow only reveals, haptic triple).
- Simulator scripts, Atlas facts and docs follow the code.

Files other issues own are left alone: ui_keys.c / keys.c (1e57) get token-level changes only, and
voice.c, sim_link.c, sim_script.c (f62b) are not edited on this branch. Issue 4843 (settings: volume,
quiet) overlaps the STATUS screen; the redesigned STATUS leaves room for a volume control and 4843
should build on it.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: ff33ff3+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T05:14:59Z · claude-code@linux-dev · linux-x86_64 · ff33ff3+dirty: filed
- 2026-11-11T05:15:03Z · claude-code@linux-dev · linux-x86_64 · ff33ff3+dirty: claimed
- 2026-11-11T06:31:31Z · claude-code@linux-dev · linux-x86_64 · facbc79+dirty: Built on branch ui-v3 (local, not pushed; HEAD f17bed3, 20 commits over main): docs/design/UI-PROFILE.md (audit) and UI-V3.md (spec, updated to what was built); Figtree + Phosphor fonts, tokens, legend, toast, ring, list/picker component; every screen but KEYS restyled; swipe right = back in six modes; row taps; nav safety rules unchanged. An independent review found and this branch fixed: a hold on a neighbour row committing the selected item, VOICE sending a transcript longer than it shows, a second tap after opening ASK committing Open on PC, AGENT hold not cancelled on selection change, answered state flashing, a sideways flick leaving SCROLL. Checks on linux-x86_64: ctest -j 16 87/87 (43-54 s; the suite was 463 s serial), atlas extract --check and 140 node tests, idf.py build 1.28 MB (79% of the app partition free), check_dram 128417 B free. Flashed to the StopWatch: clean boot log (one reset, no assert or malloc failure), daemon reconnected and sent a snapshot. Not yet done: hands-on validation of touch targets and motion on the glass, merge to main, Atlas wasm rebuild (no Emscripten here), a simulator battery knob to test the low-battery chip. Open question for the user: a permission text longer than three lines is still ellipsized and can be approved (as on main).
