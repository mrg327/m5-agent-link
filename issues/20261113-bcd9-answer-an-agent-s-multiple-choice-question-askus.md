---
id: 20261113-bcd9
title: Answer an agent's multiple-choice question (AskUserQuestion) on the watch
status: claimed
severity: med
area: daemon
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-13T01:56:36Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

Seen 2026-11-12: Claude Code asked a two-question AskUserQuestion; the watch only offered "Open on PC", so the
user could not answer away from the PC. The daemon marks the request as a question (sub question) but the
watch has no way to pick an option.

## Plan

- Hold the PermissionRequest for tool AskUserQuestion like an approval (answer window) and send the
  questions, options (label + description) and multiSelect to the watch.
- The watch shows one question at a time, options as rows; a hold picks (multi-select: toggles, then a Send
  hold). "Other": dictate or type a free answer.
- The daemon answers allow with updatedInput = original input plus `answers` keyed by question text (verify
  the exact shape against Claude Code docs and a live run before relying on it); the terminal dialog stays
  usable in parallel, first answer wins.
- Areas: protocol, daemon, firmware, simulator. Same branch as 20261113-da11.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: fce1a1d HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-13T01:56:36Z · claude-code@linux-dev · linux-x86_64 · fce1a1d: filed
- 2026-11-13T01:56:36Z · claude-code@linux-dev · linux-x86_64 · bf451bb: claimed
