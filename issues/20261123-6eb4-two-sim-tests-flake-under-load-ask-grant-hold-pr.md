---
id: 20261123-6eb4
title: Two sim tests flake under load (ask_grant_hold_progress, voice_dictate)
status: open
severity: low
area: simulator
platforms: linux
owner:
created: 2026-11-23T00:11:34Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

`ctest --test-dir build/sim -j 16` with load average ~8 (other builds running) failed sim_ask_grant_hold_progress and sim_voice_dictate; both pass alone with `ctest -R`. 227/229 otherwise. Likely wall-clock timing assumptions in the scripts. Seen while testing scripts/check (issue 20261122-db53).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 76170d3+dirty conventions
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-23T00:11:34Z · claude-code@linux-dev · linux-x86_64 · 76170d3+dirty: filed
