---
id: 20261122-4094
title: Content nits: no-folder sigils alike, overdue jobs look done, start = working, exit 0 failure, empty diff pill, one-word Read
status: open
severity: low
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

UX profile (docs/design/UX-PROFILE.md §5 C6-C8). Sessions with no folder share one sigil (content/s04_empty). A job far past its estimate shows a full ring (j01_jobs). Start looks like working (s05_states). Failed with exit 0 shows "0"; a 0/0 diff shows an empty pill (d01_done). A one-word message still offers Read and the page shows it alone (r02_word). The preview is often a markdown heading; reader pages split code blocks early (r04_md).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
