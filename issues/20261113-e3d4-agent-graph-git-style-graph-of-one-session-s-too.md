---
id: 20261113-e3d4
title: Agent graph: git-style graph of one session's tool calls and sub-agents
status: claimed
severity: med
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-13T23:31:22Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Wanted: for one agent session, a git log --graph style view on the watch: one lane for the main agent, a lane per sub-agent that forks off the parent's task/Agent call and merges back when the sub-agent stops, and one glyph per tool call (read, edit, shell, search, web, agent, ask, plan, git, test, other) coloured by its result (running / ok / failed). No text: glyphs and lanes only (project tenet, README Principles).

Contract: docs/design/GRAPH.md (proto 0.22: cmd_op_graph = 21, mt_graph = 28).
Daemon: per-session graph built from the raw hook lines (Copilot agentId/parentAgentId/toolCallId, Claude agent_id/agent_type/tool_use_id, SubagentStop).
Firmware: GRAPH screen opened from the AGENT wheel ("Graph"), drawn with one custom draw callback, rim scrolls.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 6d95e27 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T23:31:22Z · copilot-cli/opus@mac-work · macos-arm64 · 6d95e27: filed
- 2026-11-13T23:31:25Z · copilot-cli/opus@mac-work · macos-arm64 · 6d95e27: claimed
- 2026-11-13T23:58:39Z · copilot-cli/opus@mac-work · macos-arm64 · 2b92d85: Implemented: contract b96cb8b, daemon ff52ad3, firmware 2b92d85. Checks: cargo fmt/clippy/test (agentlinkd 329), sim ctest 198/198, idf build (DRAM 182041 B free), atlas 222, vectors ok. Daemon reinstalled; agentlink graph shows the live session. Pending: OTA to the watch (not connected at install time) and a live look at the screen.
