---
id: 20261114-6d07
title: cargo clippy -D warnings fails on Linux: PointerError::Denied is never constructed
status: fixed
severity: low
area: daemon
platforms: linux
owner: claude-code@linux-dev
created: 2026-11-14T03:40:14Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

On main at 88887b0, `cargo clippy --workspace -- -D warnings` fails on linux-x86_64 with
"variant `Denied` is never constructed" at daemon/crates/agentlinkd/src/pointer.rs:14 (the trackpad pointer,
20261113-61ce). Probably only the macOS path constructs it; a cfg on the variant or `#[cfg_attr(not(target_os = "macos"), allow(dead_code))]`
would fix it. The release build shows the same as a warning.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 9d039a1 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-14T03:40:15Z · claude-code@linux-dev · linux-x86_64 · 9d039a1: filed
- 2026-11-14T05:11:28Z · claude-code@linux-dev · linux-x86_64 · b4beb9d: claimed
- 2026-11-14T05:12:47Z · claude-code@linux-dev · linux-x86_64 · 1523eb7: fixed: commit 1523eb7; cfg_attr allow(dead_code) on unix-not-macos non-test builds. Checks on linux-x86_64: cargo fmt --check, cargo clippy --workspace -D warnings (with and without --all-targets), cargo test --workspace all pass
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: 1523eb7 main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
