---
id: 20261112-c629
title: Copilot CLI: gauges, done summary and more hook events from Copilot's own data
status: open
severity: low
area: daemon
platforms: any
owner:
created: 2026-11-12T22:39:01Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Split out of 20261112-2e2b (macOS bring-up), whose log has the details. On a Copilot CLI machine the
gauges (2777), standing rules (7f85) and done summary (d01e) stay empty because they read Claude Code data.

Copilot CLI 1.0.106 has these sources:
- statusLine.command in ~/.copilot/settings.json gets the session status as JSON on stdin. The
  agentlink-hook statusline shim could take a copilot mode; the stdin shape is not captured yet.
- ~/.copilot/session-state/<id>/events.jsonl: session.usage_record, session.usage_checkpoint,
  session.mode_changed, session.model_change, session.shutdown codeChanges.
- Hook events agentlink does not register yet: postToolUseFailure (hook_source already handles it),
  subagentStart/subagentStop, preCompact. permissionRequest blocks, but it fires before rules and
  session approvals, so it needs a fast pass-through before it could replace typing into kitty.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 4143bb0 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-12T22:39:02Z · copilot-cli/opus@mac-work · macos-arm64 · 4143bb0: filed
