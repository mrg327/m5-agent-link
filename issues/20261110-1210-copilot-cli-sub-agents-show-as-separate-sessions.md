---
id: 20261110-1210
title: Copilot CLI sub-agents show as separate sessions on the watch
status: open
severity: med
area: daemon
platforms: any
owner:
created: 2026-11-10T18:30:58Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Copilot CLI sub-agents (the `task` tool) fire hooks with their own `sessionId`, so each one becomes its own
session on the watch (seen: parent #2 working, a finished research sub-agent #4 and a running sub-agent #5,
all `copilot` / same repo). A parent that fans out to several sub-agents fills the ring.

Sub-agents run inside the parent's CLI process, so the hook's parent pid chain (registry.rs `pids`,
agentlink-hook main.rs) should name the same agent process as the parent's.

Proposal: in the registry, fold a new `copilot:<sessionId>` whose agent pid matches a live session's agent pid
into that session. Its steps go into the parent's timeline (prefixed with the sub-agent's name if the payload
carries one), and it never gets a ring segment of its own. Its `done` must not mark the parent done. To check
first: dump one sub-agent hook payload, which may already carry a parent/agent field, and confirm the pid
chains match. Claude Code sub-agents (SubagentStop) may need the same treatment.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 9c5bd1c main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T18:30:59Z · copilot-cli/opus@mac-work · macos-arm64 · 9c5bd1c: filed
- 2026-11-10T18:44:18Z · copilot-cli/opus@mac-work · macos-arm64 · 17020b3+dirty: User report: every sub-agent adds an extra agent to the HOME deck, but selecting it does nothing useful (it has no terminal of its own, so Focus/Reply/Watch don't apply), which is confusing. Acceptance: sub-agents never appear as their own HOME entries; at most their activity shows inside the parent's timeline. Raising severity to med.
