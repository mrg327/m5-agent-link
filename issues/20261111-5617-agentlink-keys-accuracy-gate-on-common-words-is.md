---
id: 20261111-5617
title: agentlink-keys accuracy_gate_on_common_words is flaky under load
status: fixed
severity: low
area: daemon
platforms: linux
owner: copilot-cli/opus@mac-work
created: 2026-11-11T01:42:37Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: macos-arm64
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
- 2026-11-13T20:05:50Z · copilot-cli/opus@mac-work · macos-arm64 · 5e62153+dirty: fixed: commit 5e62153; repro: unoptimised build + 36 CPU hogs failed 3/3 at gate.rs:39 before, passed 3/3 after; checks: fmt, clippy -D warnings, agentlink-keys 8 passed, agentlinkd keys 13 passed
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 5e62153+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
