---
id: 20261110-fab5
title: Atlas wasm build fails to link: web_hal.c lacks hal_mic_* and hal_reset_reason
status: open
severity: low
area: atlas
platforms: any
owner:
created: 2026-11-10T21:41:15Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

`atlas/wasm/build.sh` fails at link time: `wasm-ld: undefined symbol: hal_reset_reason` (link_rx.c) and `hal_mic_start/stop/read/error` (voice.c). The core grew these HAL calls, but `atlas/wasm/web_hal.c` never got stubs, so `atlas/vendor/watch.{js,wasm}` can't be rebuilt. Fix: add no-op stubs (has_mic is false there), then rebuild. Also note that a cached build dir can point FETCHCONTENT_SOURCE_DIR_LVGL at another worktree; pass LVGL_SRC=$PWD/build/sim/_deps/lvgl-src.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: b84fdea+dirty main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T21:41:16Z · copilot-cli/opus@mac-work · macos-arm64 · b84fdea+dirty: filed
