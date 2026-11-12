---
id: 20261112-a9d5
title: Longer answer window with countdown, and full text for long permission requests
status: claimed
severity: med
area: protocol
platforms: any
owner: claude-code@linux-dev
created: 2026-11-12T00:32:54Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

The hook hands the prompt back to the terminal after 15 s by default; make the window configurable with a visible countdown, and build the planned detail request so long requests can be read in full and approved. Touches protocol, daemon and firmware. Branch next.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 6d098e8 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-12T00:32:54Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: filed
- 2026-11-12T00:32:54Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: claimed
- 2026-11-12T02:41:33Z · claude-code@linux-dev · linux-x86_64 · c950d64: Built on branch next (7b5192f..70ecb34): protocol 0.11, answer window default 60 s (5-300 s, setting), countdown on ASK, handed_back clear reason, detail chunks up to 4096 bytes with approve locked until read. answer_window_e2e 30/30, ctest 136/136 at that point. Not verified in a live session.
