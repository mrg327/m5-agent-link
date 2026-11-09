---
id: 20261109-e9b2
title: Remote sessions: live test from Windows 11 over Win32-OpenSSH
status: open
severity: med
area: daemon
platforms: windows,macos
owner:
created: 2026-11-09T19:15:35Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

Run agentlink-hook.exe on a Windows 11 machine with AGENTLINK_REMOTE set, through agentlink ssh from the Mac. Check that sessions show the host name on the watch, that ask and approve round-trips work, and that focus and scroll raise the local attach window. Also confirm Win32-OpenSSH -R to a unix socket path, and the RDP case (docs/design/REMOTE.md section 6).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 24f47bf main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T19:15:35Z · copilot-cli/opus@mac-work · macos-arm64 · 24f47bf: filed
