---
id: 20261123-2436
title: Conventions phase 1: formatters, one reformat per language, docs checks, CI on scripts/check
status: claimed
severity: low
area: ci
platforms: any
owner: claude-code@linux-dev
created: 2026-11-23T03:45:06Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Phase 1 of docs/conventions (README "Phases").
- .clang-format (tooling.md §1 candidate, with the decisions in c.md) and one reformat of first-party C/C++; `//` comments turned into `/* */`.
- ruff.toml (format, 100 columns) and one Python reformat; .prettierrc.json (singleQuote, 100) and one JS/TS reformat; rustfmt.toml (already clean).
- Each reformat commit listed in .git-blame-ignore-revs.
- scripts/check gets a `format` area and a real `docs` area (typos, relative-link check, markdownlint on our own docs).
- CI workflows call scripts/check.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 300d480 conventions
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-23T03:45:06Z · claude-code@linux-dev · linux-x86_64 · 300d480: filed
- 2026-11-23T03:45:10Z · claude-code@linux-dev · linux-x86_64 · 300d480: claimed
