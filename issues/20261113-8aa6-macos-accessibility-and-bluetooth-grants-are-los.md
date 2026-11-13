---
id: 20261113-8aa6
title: macOS: Accessibility (and Bluetooth) grants are lost on every daemon rebuild
status: claimed
severity: med
area: daemon
platforms: macos
owner: copilot-cli/opus@mac-work
created: 2026-11-13T22:24:26Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

launchd.sh signs agentlinkd ad hoc, so its designated requirement is the cdhash of that build. After cargo install + launchd.sh install, System Settings still shows agentlinkd switched on under Accessibility, but the grant no longer matches the binary: the trackpad says to allow agentlinkd although it is allowed. Fix: sign with a stable self-signed code-signing identity so the requirement is identifier + certificate and survives rebuilds.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 2c6c4ef main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T22:24:26Z · copilot-cli/opus@mac-work · macos-arm64 · 2c6c4ef: filed
- 2026-11-13T22:24:32Z · copilot-cli/opus@mac-work · macos-arm64 · 2c6c4ef: claimed
- 2026-11-13T22:26:36Z · copilot-cli/opus@mac-work · macos-arm64 · 3f9ee55: launchd.sh cert + install on mac-work: designated requirement is now identifier + certificate leaf (was cdhash). Waiting for the user to re-grant Accessibility once and confirm it survives the next rebuild.
