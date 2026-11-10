---
id: 20261110-b150
title: Quick replies and interrupt from the watch
status: claimed
severity: med
area: protocol
platforms: any
owner: claude-code@linux-dev
created: 2026-11-10T05:19:05Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

User decision 2026-11-09: first input path from the watch. A short list of canned replies from the
daemon config ("Continue", "Yes", "Run the tests"…) offered on a session that waits for input, typed
into its terminal through the same path as Copilot prompts (prompt.rs send_keys, after a screen
check). Plus a hold item "Interrupt" on a working session that sends Esc. Needs a protocol op,
welcome feature flag, daemon config, firmware items, sim tests. Never types while a permission
prompt is on screen.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 9d39d6d main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-10T05:19:05Z · claude-code@linux-dev · linux-x86_64 · 9d39d6d: filed
- 2026-11-10T05:19:11Z · claude-code@linux-dev · linux-x86_64 · 9d39d6d: claimed
