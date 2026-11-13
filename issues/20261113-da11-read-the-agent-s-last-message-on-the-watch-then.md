---
id: 20261113-da11
title: Read the agent's last message on the watch, then reply or dictate
status: claimed
severity: med
area: protocol
platforms: esp32,linux
owner: claude-code@linux-dev
created: 2026-11-13T01:56:16Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

## Summary

Away from the PC, the user cannot see what the agent said when it finished, so they cannot answer it by
dictation from across the room. The daemon already receives the whole final message (Claude Code Stop hook,
`last_assistant_message`) but only the closing question (≤ 120 chars, ask.rs) reaches the watch.

## Plan (user's choices, 2026-11-12)

- Final message only (not the running text between tool calls).
- No pop-up: the finished card shows the first lines and a Read action; Read opens the full message in the
  paged reader (detail chunks, as for long permission text, up to 4 KB, markdown cleaned); Reply and Dictate
  are at the end of the message.
- Protocol 0.18. Respect privacy modes (agent content: never in audit or trace; `meta` sends no text).
- Areas: protocol, daemon, firmware, simulator. Copilot CLI's stop hook has no final message: out of scope here.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: fce1a1d HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-13T01:56:16Z · claude-code@linux-dev · linux-x86_64 · fce1a1d: filed
- 2026-11-13T01:56:36Z · claude-code@linux-dev · linux-x86_64 · bf451bb: claimed
