---
id: 20261116-f705
title: Support the pi coding agent (sessions, steps, done summary, last message)
status: claimed
severity: med
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-16T21:54:17Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

User request (2026-11-16): the user runs the pi coding agent (pi.dev, npm @earendil-works/pi-coding-agent) on
the work Mac and wants it on the watch like Claude Code and Copilot CLI.

pi has no shell hooks; it has TypeScript extensions (`~/.pi/agent/extensions/*.ts`) with session, prompt,
tool, `agent_settled` and `ui_prompt_*` events, a blocking `tool_call`, and `pi.sendUserMessage`.

Phase 1 (this issue):
- protocol 0.23: `agent_pi = 4`; a device below 0.23 gets `agent_other` (its zcbor decoder rejects an unknown
  agent value, which would drop the whole frame).
- an extension that forwards pi's events as hook lines through `agentlink-hook pi <Event>` (same transports:
  local socket, remote, Windows pipe), in order, never holding pi up.
- daemon: pi sessions with state, steps, the done summary and the last message.
- firmware: the agent's name.

Later: replies / dictation into pi through the extension (`pi.sendUserMessage`, no terminal typing), questions
answered from the watch, an opt-in approval gate on `tool_call`.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: abef8f7 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-16T21:54:18Z · copilot-cli/opus@mac-work · macos-arm64 · abef8f7: filed
- 2026-11-16T21:54:21Z · copilot-cli/opus@mac-work · macos-arm64 · abef8f7: claimed
