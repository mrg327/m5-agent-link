---
id: 20261126-dff9
title: Revise the sigil system (later)
status: open
severity: low
area: firmware
platforms: any
owner:
created: 2026-11-26T04:41:15Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Owner request: revisit the project sigil system later. Scope to be set by the owner when it is picked up.

What exists (GLYPHS.md §3.1 and §4): three concentric rings of arcs on an 8-sector grid plus a core dot or bar, all from the first 10 bytes of SHA-256 of the repo name (`ui_sigil_*` in ui_kit.c, the port of `Scr.sigil()` in docs/design/glyphs/mock.py). An empty name draws the dashed-folder glyph. Sizes in use: r 22 on HOME and the ambient face, r 52 as the subject of an orbit.

Inputs gathered so far:
- UX audit (2026-11-26): sigils are not yet learned, so a new user cannot tell projects apart; proposed a 1.5 s project-name flash when the selection changes.
- Fun research: sigil "temperament" (breathing rhythm from the hash) and a three-note per-project motif build on the sigil as the watch's personality.
- Issue 20261122-4094: sessions with no folder all share one sigil.
- Open questions: how distinct are sigils at r 22 across many repos (collision and similarity rate), colour or no colour, legibility on the dim ambient face, and whether the user can pin or override a project's sigil.

Research notes (session scratchpad, not in the repo): ux-research/SYNTHESIS.md item 6, audit.md, fun.md §5a.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 8a1782d claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T04:41:15Z · claude-code/cloud@cloud · linux-x86_64 · 8a1782d: filed
