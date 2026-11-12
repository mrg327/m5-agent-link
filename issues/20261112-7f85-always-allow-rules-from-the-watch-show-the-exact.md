---
id: 20261112-7f85
title: Always-allow rules from the watch: show the exact rule and scope before the hold
status: claimed
severity: med
area: protocol
platforms: any
owner: claude-code@linux-dev
created: 2026-11-12T08:04:20Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

A third answer to a permission request that grants a standing rule (for example always allow a command in this project), with the exact rule and where it is saved shown on the watch before the hold. Touches protocol, daemon and firmware. Branch next.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 997305d HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-12T08:04:20Z · claude-code@linux-dev · linux-x86_64 · 997305d: filed
- 2026-11-12T08:04:21Z · claude-code@linux-dev · linux-x86_64 · 997305d: claimed
- 2026-11-12T08:52:47Z · claude-code@linux-dev · linux-x86_64 · 3680681: Built on branch next (bae0b12..b33082f): protocol 0.13 (attention rule/scope/wide, cmd rh). One Claude Code suggestion passed back verbatim, addRules/allow only, session or local project scope, exact rule shown before a 1.6 s hold, grant bound to the rule shown by hash. Before this the hook echoed the whole suggestions array unseen. standing_rules_e2e 31/31. Not verified in a live session; not deployed to the daemon yet.
