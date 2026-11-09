---
id: 20261109-7dc6
title: Re-sent attention wipes the answer already given
status: fixed
severity: low
area: firmware
platforms: any
owner: copilot-cli/sonnet-fw@mac-work
created: 2026-11-09T17:02:25Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

model_attention_add replaces a same-rid slot wholesale, clearing answer_sent/answer/answer_opt; a daemon re-send (reconnect, lag resend) re-opens the options and allows a duplicate answer.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 48dafed main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, rustc 1.99.1, cargo 1.99.1, sdl2 2.32.74

## Log

- 2026-11-09T17:02:25Z · copilot-cli/opus@mac-work · macos-arm64 · 48dafed: filed
- 2026-11-09T17:03:35Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 830f290: claimed
- 2026-11-09T17:08:35Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 61369e5+dirty: fixed: checks: ctest 46/46 (no dedicated test)
    - agent: copilot-cli/sonnet-fw
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 61369e5+dirty main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
