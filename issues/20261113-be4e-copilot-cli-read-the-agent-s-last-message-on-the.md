---
id: 20261113-be4e
title: Copilot CLI: read the agent's last message on the watch
status: fixed
severity: med
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T17:43:07Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

da11 brought the agent's final message to the watch (Read on the finished card, then Reply or Dictate) for Claude Code only: Claude's Stop hook carries `last_assistant_message`, Copilot CLI's `agentStop` does not.

Copilot CLI 1.0.106's `agentStop` input does carry `transcriptPath` (the session's events.jsonl), and the final message is already written there when the hook fires:
- an `assistant.message` with non-empty `content` (normal turns), or
- a `session.task_complete` with `summary` (autopilot: the model ends with the task_complete tool).

Plan: agentlink-hook reads the tail of the transcript on `agentStop` and passes the newest of those since the last `user.message` as the last message, so the daemon, protocol and watch need no change. Bounded read (tail only), no failure if the file is missing or malformed.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 2a4ae8e main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T17:43:07Z · copilot-cli/opus@mac-work · macos-arm64 · 2a4ae8e: filed
- 2026-11-13T17:43:11Z · copilot-cli/opus@mac-work · macos-arm64 · 2a4ae8e: claimed
- 2026-11-13T17:48:10Z · copilot-cli/opus@mac-work · macos-arm64 · 779794c: fixed: commit 779794c; cargo fmt --check, clippy -D warnings, cargo test --workspace pass on macos-arm64; live: real Copilot CLI agentStop payload through the installed hook set lm/ll/lp on the session in agentlinkd. Not yet read on the watch (it was asleep).
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 779794c main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
