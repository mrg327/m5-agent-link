---
id: 20261111-f6fc
title: ASK: read before you approve (long permission text)
status: claimed
severity: med
area: firmware
platforms: any
owner: claude-code@linux-dev
created: 2026-11-11T08:16:10Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

A permission text longer than three lines is ellipsized on ASK and can still be approved by the hold, so the
part of a command that is not on screen can be approved unseen (found in the UI v3 safety review, issue 7732).
The daemon also clips the text to 160 characters (broker.rs, `clip`), so the tail of a longer command never
reaches the watch at all.

Proposal, on branch `ui-v3-read-first`: a text that does not fit shows two lines and "Read all"; a tap opens a
paged reader; nothing that grants permission can be held until the last page has been on screen; a text the
daemon cut can be read and denied on the watch but only approved on the PC. Follow-up, not in this branch:
implement the `detail` op (PROTOCOL §attention) so the watch can fetch and page the full text.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 0104ec1 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T08:16:11Z · claude-code@linux-dev · linux-x86_64 · 0104ec1: filed
- 2026-11-11T08:16:11Z · claude-code@linux-dev · linux-x86_64 · 0104ec1: claimed
- 2026-11-11T08:16:25Z · claude-code@linux-dev · linux-x86_64 · 958e24a: Built on branch ui-v3-read-first: reader, approve lock, three sim tests (ask_read_first, ask_read_then_approve, ask_cut_text). ctest 90/90 on linux-x86_64; firmware builds, check_dram 128345 B free. Not flashed, not merged: waiting for the owner's decision, in particular on blocking daemon-cut text.
