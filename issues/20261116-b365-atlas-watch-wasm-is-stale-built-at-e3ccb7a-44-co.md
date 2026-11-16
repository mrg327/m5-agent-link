---
id: 20261116-b365
title: Atlas watch.wasm is stale: built at e3ccb7a, ~44 core commits behind
status: claimed
severity: med
area: atlas
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-16T18:46:30Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

`atlas/vendor/watch.wasm` + `watch.js` were last rebuilt at e3ccb7a (2026-11-11). Since then
`firmware/components/core` gained the GRAPH screen, OTA, two BLE ports, trackpad/experiments,
STATUS icons + Volume, queued messages, jobs (proto 0.9 → 0.21). The facts check passes, but
the live watch in the Atlas runs old firmware. CI does not rebuild or check the WASM.
Fix: rerun `atlas/wasm/build.sh` and commit the vendor artifacts.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: cb16510 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-16T18:46:30Z · copilot-cli/opus@mac-work · macos-arm64 · cb16510: filed
- 2026-11-16T18:46:33Z · copilot-cli/opus@mac-work · macos-arm64 · cb16510: claimed
