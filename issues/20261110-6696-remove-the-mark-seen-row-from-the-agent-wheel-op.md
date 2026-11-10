---
id: 20261110-6696
title: Remove the Mark seen row from the AGENT wheel; opening the wheel on a Done session acknowledges it
status: fixed
severity: low
area: firmware
platforms: any
owner: copilot-cli/sonnet-fw@mac-work
created: 2026-11-10T18:22:16Z
reporter: copilot-cli/sonnet-fw@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

"Mark seen" in the AGENT wheel is pointless. Remove it from the Done item list (Reply… · Watch remain). Opening the AGENT wheel on a Done, unseen session sends ack once instead. Dismiss error keeps sending ack. Update UI-V2.md and simulator scripts.

## Environment (reporter)

- agent: copilot-cli/sonnet-fw
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 8e413c2 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T18:22:16Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 8e413c2: filed
- 2026-11-10T18:30:39Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 8e413c2+dirty: claimed
- 2026-11-10T18:30:39Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 0ac756d: fixed: commit 0ac756d; ctest 63/63 (core_ram_budget skipped); idf build + check_dram OK; atlas node tests: only voice vectors fail (other agent's work)
    - agent: copilot-cli/sonnet-fw
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 0ac756d main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
