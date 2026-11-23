---
id: 20261123-8c17
title: pi integration test: directory form fails on Node 22
status: fixed
severity: low
area: ci
platforms: linux
owner:
created: 2026-11-23T00:11:34Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

daemon.yml runs `node --test daemon/integrations/pi/` on Node 24. On Node 22.23 the directory form fails with MODULE_NOT_FOUND; `node --test daemon/integrations/pi/*.test.ts` passes (6/6). scripts/check SKIPs the step below Node 24 for now. Either switch CI and scripts/check to the glob form or document Node 24 as required.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 76170d3+dirty conventions
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-23T00:11:34Z · claude-code@linux-dev · linux-x86_64 · 76170d3+dirty: filed
- 2026-11-23T04:43:12Z · claude-code@linux-dev · linux-x86_64 · 9782863: fixed: commit 135ecf6: scripts/check and daemon.yml run node --test daemon/integrations/pi/*.test.ts (Node 22 and 24); pi 6/6
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: 9782863 conventions
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
