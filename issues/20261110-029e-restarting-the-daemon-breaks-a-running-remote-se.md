---
id: 20261110-029e
title: Restarting the daemon breaks a running remote session, which then has to be restarted
status: open
severity: low
area: daemon
platforms: any
owner:
created: 2026-11-10T20:41:45Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Restarting agentlinkd while a remote session (`agentlink ssh <remote>`) is running breaks that
session: it stops reaching the watch, and the user has to end the remote session and start
it again to get it back.

Expected: a remote session survives a daemon restart. Its hooks reconnect through the existing
tunnel once the daemon is back, and the session reappears on the watch, the same way local
sessions do.

Likely area (not yet investigated): the ssh `-R` forward points at the daemon's remote socket
(`~/.agentlink/remote.sock`). A restart removes and recreates that socket, or the session's
auth token and registration live only in the old daemon's memory, so the remote hook is refused
or never re-registers. See docs/design/REMOTE.md.

Repro: start a remote session with `agentlink ssh <name>`, restart agentlinkd, then run
anything in the remote agent that fires a hook.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: a5a6da5 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T20:41:45Z · copilot-cli/opus@mac-work · macos-arm64 · a5a6da5: filed
