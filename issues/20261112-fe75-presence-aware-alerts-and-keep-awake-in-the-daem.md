---
id: 20261112-fe75
title: Presence-aware alerts and keep-awake in the daemon
status: claimed
severity: med
area: daemon
platforms: any
owner: claude-code@linux-dev
created: 2026-11-12T00:32:47Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Do not buzz for things the user can see at the keyboard; keep the computer awake while agents or jobs run. Branch next.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 6d098e8 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-12T00:32:47Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: filed
- 2026-11-12T00:32:55Z · claude-code@linux-dev · linux-x86_64 · 6d098e8: claimed
- 2026-11-12T02:41:33Z · claude-code@linux-dev · linux-x86_64 · c950d64: Built on branch next (0b97473..408a196): protocol 0.12 hush flag, presence rule table (answer window 2 s focused / 10 s at keyboard / full when away), keep-awake idle inhibitor (ac default). presence_e2e 41/41, cargo test 273 passed, ctest 139/139. Present-at-keyboard branch tested with an injected source only; macOS and Wayland untested.
