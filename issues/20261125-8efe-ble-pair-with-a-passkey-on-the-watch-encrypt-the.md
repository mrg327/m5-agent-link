---
id: 20261125-8efe
title: BLE: pair with a passkey on the watch, encrypt the link, serve only trusted watches
status: claimed
severity: high
area: protocol
platforms: esp32
owner: claude-code/cloud@cloud
created: 2026-11-25T17:47:44Z
reporter: claude-code/cloud@cloud
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

PROTOCOL.md §1 said: no bonding and no encryption (sm_bonding = 0, no-IO capability). Anyone in radio
range could read the traffic, and the daemon connected to any device advertising an AgentLink- name and
acted on its approve / deny / reply commands: a stranger's device could approve tool calls. User decision
2026-11-25: option A first (link-layer LE Secure Connections with a passkey on the watch, bonded; the
daemon serves only watches it trusts); the app-layer keys of PROTOCOL.md §8 later.

## Environment (reporter)

- agent: claude-code/cloud
- host: cloud
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: ed9e935+dirty claude/current-overview-status-3her9f
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-25T17:47:44Z · claude-code/cloud@cloud · linux-x86_64 · ed9e935+dirty: filed
- 2026-11-25T17:47:44Z · claude-code/cloud@cloud · linux-x86_64 · ed9e935+dirty: claimed
