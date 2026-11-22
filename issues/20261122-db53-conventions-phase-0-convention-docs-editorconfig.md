---
id: 20261122-db53
title: Conventions phase 0: convention docs, .editorconfig, scripts/check
status: claimed
severity: low
area: docs
platforms: any
owner: claude-code@linux-dev
created: 2026-11-22T23:47:12Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Phase 0 of the engineering-conventions effort (branch conventions).

Write docs/conventions/ (one short page per language plus SDLC/docs and protocol naming, citing the upstream guide rather than restating it), add .editorconfig, add scripts/check as the one entry point wrapping what CI runs today, and list the check commands in AGENTS.md. No reformatting or code changes in this phase.

Decisions: C 100 columns, `char *p`, `/* */` comments only, 60-line function cap (baseline for existing); esp_err_t-style errors in all C modules; Rust bans unwrap and expect outside tests (#[expect(reason)] for exceptions); Linux-style `area: summary (issue ID)` commits; Prettier for JS; CODEOWNERS advisory.

Also test before enforcing: clang-tidy naming for s_ statics, idf.py clang-check on the ESP32-S3 toolchain, Doxygen warn-as-error with INPUT limited to public headers.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 51f1a77 conventions
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T23:47:12Z · claude-code@linux-dev · linux-x86_64 · 51f1a77: filed
- 2026-11-22T23:47:16Z · claude-code@linux-dev · linux-x86_64 · 51f1a77: claimed
