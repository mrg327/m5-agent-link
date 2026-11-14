---
id: 20261113-203f
title: Delta OTA: the main loop stalls for seconds while a chunk rebuilds image bytes
status: fixed
severity: low
area: firmware
platforms: esp32s3
owner: claude-code@linux-dev
created: 2026-11-13T20:47:34Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: linux-x86_64
gh:
---

## Summary

With a delta update (ef2c) a 2 KB patch chunk can rebuild hundreds of KB of image, all written to flash inside ota_on_chunk on the main loop. Live: the first chunks took 6.0 s and 4.4 s, so the UI freezes and acks lag. Progress is counted in patch bytes, so the screen jumps 0 -> 49 -> 99%. Ideas: bound the work per loop pass (feed the decoder in slices and ack when the slice is done), and show progress as image bytes written (s.out / s.size).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 3741845 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-13T20:47:34Z · copilot-cli/opus@mac-work · macos-arm64 · 3741845: filed
- 2026-11-14T02:23:58Z · claude-code@linux-dev · linux-x86_64 · 88887b0: claimed
- 2026-11-14T03:18:08Z · claude-code@linux-dev · linux-x86_64 · 875d368: Live over BLE from linux-dev (BCM4350C0), daemon at main: delta 3920ea2 -> 3920ea2-dirty (60 KB patch) with the old decoder: 26 s end to end, the first chunk held the main loop 3.3 s. First fix (ack a chunk once applied) failed live: a 4 KB patch whose first chunk rebuilds 1.3 MB went 10 s without an ack and the daemon gave up. Reworked: patch kept whole in PSRAM, acked on arrival, keepalive go every 3 s once all is in. Full image 3920ea2-dirty -> 875d368 over BLE: 125 s.
- 2026-11-14T03:36:11Z · claude-code@linux-dev · linux-x86_64 · 4c641d4: The first fix rebuilt more slowly (19-20 s for 1.3 MB vs 11-12 s) because the Updating overlay redrew the whole screen on every percent; 4c641d4 redraws only the arc's new stretch. Measuring next.
- 2026-11-14T03:40:14Z · claude-code@linux-dev · linux-x86_64 · 9d039a1: fixed: commits 0beaa46 (slices + whole patch in PSRAM + keepalive), 4c641d4 (partial arc redraw). Live over BLE from linux-dev, delta 4c641d4 -> 4c641d4-dirty (4 KB patch, 1.3 MB rebuilt): longest main-loop step 105 ms (was 3.3 s), 122 steps, rebuild 16.2 s, 4 keepalives, 30 s end to end; earlier builds of the fix went 0beaa46-dirty -> c32851b and 875d368 -> 875d368-dirty. Clean boot after each. Checks: sim ctest 203/203 (new: ota_patch_*_slow, ota_patch_*_keepalive, ota_redraw), idf build, check_dram 181905 B free, atlas --check, cargo fmt + test --workspace (new window keepalive test). cargo clippy -D warnings fails on main before this change (pointer.rs Denied unused on linux; filed). Trade-off: the rebuild is slower than with a frozen screen (16 s vs 11-12 s) because the UI keeps drawing. Old daemons work unchanged (no protocol change); not yet run from the macOS daemon.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: 9d039a1 main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
