---
id: 20261122-6b60
title: Text outside Latin-1 draws as empty boxes (CJK, Cyrillic, Greek, Polish, emoji): approval text the user cannot read
status: claimed
severity: med
area: daemon
platforms: any
owner: claude-code/strings@linux-dev
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §5 C1). The fonts cover 0x20-0x7E and 0xA0-0xFF. The daemon replaces undrawable characters only in the last message (lastmsg::drawable); session, attention (title, text, options), question and step text reach the watch raw, so ASK can ask for a hold-approve on a command shown as boxes (a commit message with an emoji is enough).
Repro: simulator/scripts/ux (branch ux-profile)/content/s03_scripts.txt, a08_cjk.txt, q02_cjk.txt, w01_watch.txt.
Should: the same stand-in/drop pass for every string sent to the watch, and a visible sign that text was not shown (with Open on PC / Read all), or wider glyph coverage.

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
