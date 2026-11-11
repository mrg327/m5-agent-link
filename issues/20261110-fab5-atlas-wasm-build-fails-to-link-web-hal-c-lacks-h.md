---
id: 20261110-fab5
title: Atlas wasm build fails to link: web_hal.c lacks hal_mic_* and hal_reset_reason
status: fixed
severity: low
area: atlas
platforms: any
owner: claude-code@linux-dev
created: 2026-11-10T21:41:15Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: linux-x86_64
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
- 2026-11-11T01:07:46Z · claude-code@linux-dev · linux-x86_64 · 2719b23: claimed
- 2026-11-11T01:08:34Z · claude-code@linux-dev · linux-x86_64 · 1e41702+dirty: fixed: this commit: web_hal.c stubs for hal_reset_reason and hal_mic_start/read/stop/error (has_mic false: dictation shows 'No microphone'). Checked by symbol: all 28 hal_* calls in libcore are now defined in web_hal.c. NOT rebuilt here (no Emscripten on linux-dev): atlas/vendor/watch.{js,wasm} still needs atlas/wasm/build.sh on a machine with emcc (LVGL_SRC tip from the report).
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: 1e41702+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
