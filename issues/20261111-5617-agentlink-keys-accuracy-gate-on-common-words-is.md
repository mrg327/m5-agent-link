---
id: 20261111-5617
title: agentlink-keys accuracy_gate_on_common_words is flaky under load
status: claimed
severity: low
area: daemon
platforms: linux
owner: copilot-cli/opus@mac-work
created: 2026-11-11T01:42:37Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

On linux-x86_64 (4 cores), `cargo test --workspace` failed `accuracy_gate_on_common_words`
(crates/agentlink-keys/tests/gate.rs:39) twice while other test binaries ran (14.1 s), and passed 3 of
3 runs alone (10.5 s), with no change to that crate. Looks time- or load-dependent; worth making the
gate deterministic or independent of wall-clock budgets.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: b7d78b6+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T01:42:37Z · claude-code@linux-dev · linux-x86_64 · b7d78b6+dirty: filed
- 2026-11-13T19:59:20Z · copilot-cli/opus@mac-work · macos-arm64 · 5470781: claimed
