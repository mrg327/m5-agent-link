---
id: 20261116-f705
title: Support the pi coding agent (sessions, steps, done summary, last message)
status: fixed
severity: med
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-16T21:54:17Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
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
- 2026-11-16T22:05:10Z · copilot-cli/opus@mac-work · macos-arm64 · 0253ee0: fixed: commit 0253ee0. Checks: daemon fmt + clippy --all-targets -D warnings + test --workspace pass (incl. a_pi_turn_shows_its_steps_summary_and_last_message, pi_tools_count_files_lines_and_tests, a_pi_session_reaches_an_older_device_as_other); node --test daemon/integrations/pi 6/6; protocol validate/gen vectors clean (+session_pi); simulator ctest pass; atlas extract --check + 224/224; WASM rebuilt; idf.py build + check_dram (185785 B free); flashed to StopWatch over USB, boot log clean, hello 0.23. E2E: pi -p -e agentlink.ts edited a file and ran bash; the watch got ag=pi, steps, done fc1 la1 lr1 dur15 + last message, gone; no rejects. Later phases (replies, questions, approval gate, remote) in docs/design/PI.md.
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 0253ee0 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
