---
id: 20261109-f545
title: SCROLL should feel like a trackpad: fine steps, immediate send, momentum
status: fixed
severity: med
area: firmware
platforms: any
owner: copilot-cli/sonnet-fw@mac-work
created: 2026-11-09T17:25:13Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

SCROLL feels like "enter and send": 24 px quantised steps of 3 lines, the first command waits out the
50 ms coalesce window, and a flick jumps a whole page. The user wants it to behave like a trackpad or
scroll wheel: the terminal moves with the finger, immediately, and keeps gliding after a flick.

Fix: 1 line per ~10 px of drag (gated by tap slop so a tap still exits), the first step of a burst
sent at once (leading edge) and the rest coalesced, a detent per 3 lines (one wheel notch) instead
of per step, and momentum after release (exponential decay; a new touch stops it, as do the ends of
the scrollback). Flick no longer means "page"; button B still pages.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 955c3ab main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:25:13Z · copilot-cli/opus@mac-work · macos-arm64 · 955c3ab: filed
- 2026-11-09T17:25:16Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 955c3ab: claimed
- 2026-11-09T17:36:05Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · d3484eb+dirty: Trackpad SCROLL implemented: 1 line per 10 px drag gated by the 18 px tap slop (stream starts at slop crossing); leading-edge send then 40 ms coalesce; detent per 3 finger lines; new G_DRAG_END with release vy (px/s, last 80 ms, any drag length) -> NAV_SCROLL_FLING glide (start >=300, cap 3000, tau 400 ms, stop <60 px/s or 2.5 s; stopped by any press (NAV_SCROLL_CATCH), A/B, leaving, bottom/top); a tap that catches a glide does not leave; flick no longer pages (SCROLL swipe rows removed, NAV_PAGE_OLDER dropped). Tests: test_gesture updated (slop tap, 10 px steps, release velocity), scroll_mode asserts leading edge, new sim_scroll_momentum. ctest 47/47 on macos-arm64; idf.py device build OK. UI-V2 §3/§4.4/§5 updated; §8a/§9 item 15/§8.3 Phase-4 row still mention flick=page (outside my edit scope).
- 2026-11-09T17:43:11Z · copilot-cli/opus@mac-work · macos-arm64 · 32ae514: fixed: commit 0d26139; sim ctest 47/47 (new sim_scroll_momentum), atlas 90/90 + extract --check, idf.py build OK. Glide constants tuned in the simulator only; verify feel on the watch.
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 32ae514 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
