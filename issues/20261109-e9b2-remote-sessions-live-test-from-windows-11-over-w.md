---
id: 20261109-e9b2
title: Remote sessions: live test from Windows 11 over Win32-OpenSSH
status: fixed
severity: med
area: daemon
platforms: windows,macos
owner: copilot-cli/opus@mac-work
created: 2026-11-09T19:15:35Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
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
- 2026-11-09T20:08:33Z · copilot-cli/opus@mac-work · macos-arm64 · 84907c2+dirty: claimed
- 2026-11-09T20:08:33Z · copilot-cli/opus@mac-work · macos-arm64 · 84907c2+dirty: Win11 26200 / Win32-OpenSSH 9.5, hook cross-compiled with cargo-xwin: tunnel (-R TCP to Mac unix socket), token auth (os=win), session shown with host and attached to the kitty window, hooks 29-457 ms, attach socket removed on exit. All pass. Blocked: Copilot CLI over a key-auth ssh logon cannot read saved sign-in (Credential Manager is not available), so the user must authenticate in that session. Quirks recorded in REMOTE.md section 4: admin authorized_keys location, PowerShell default shell. Remaining: interactive session, approve/deny, tunnel drop, port in use, focus/scroll.
- 2026-11-09T20:28:19Z · copilot-cli/opus@mac-work · macos-arm64 · 12225ed: Saved remotes added (remote add/setup, ssh <name>). Live on the Windows 11 host: remote add --setup ran all 4 steps cleanly; an on-connect probe started in the saved dir, its hook reached the watch over the tunnel, and ssh closed when the command exited. Known: with PowerShell as the default ssh shell the exit code collapses to 0/1. Still open: approve/deny from the watch, focus/scroll, tunnel drop, port in use.
- 2026-11-09T20:39:25Z · copilot-cli/opus@mac-work · macos-arm64 · 70783ba: Linux/WSL remotes: remote setup supports --os unix (static musl hook via cargo-zigbuild). Live on a WSL box (sshd on a non-default port): setup ran all 4 steps and re-ran without a password; on-connect started in ~/rfm_virt with the profile's port; the hook reached the watch over the tunnel showing the real host name; ssh returned the command's exit code.
- 2026-11-13T19:33:47Z · copilot-cli/opus@mac-work · macos-arm64 · ed9c889: fixed: user confirmed on the watch, 2026-11-13
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: ed9c889 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
