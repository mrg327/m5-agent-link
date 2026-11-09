---
id: 20261109-a059
title: Scroll does nothing in agent TUIs (kitty alternate screen); 2-3 rc round trips per batch
status: fixed
severity: high
area: daemon
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-09T17:25:12Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

Agent TUIs (Copilot CLI, Claude Code) run on kitty's alternate screen, which has no scrollback.
`kitty @ scroll-window` therefore never moves them: the trace shows `scroll delta:-6 → lines_above:0
at_bottom:true` on every batch, and the watch counter snaps back to "Live". Each batch also costs
two or three `kitty @` calls (scroll-window, ls, get-text).

Fix: one `kitten @ kitten <agentlink_wheel.py>` call per batch that does what kitty does for a
physical wheel (kitty/mouse.c scroll_event): main screen → history scroll (exact scrolled_by and
history size); alternate screen with mouse tracking → SGR wheel button 4/5 via send_mouse_event at
the window centre; alternate screen without tracking → up/down keys (kitty's fake_scroll). It
reports position and preview in the same round trip. Fall back to scroll-window when the kitten
command is refused. Measured round trip: about 24 ms. Verified in scratch kitty tabs: SGR
`\e[<64;…M`, `\e[A`, and main-screen scrolled_by all as expected.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 955c3ab main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T17:25:13Z · copilot-cli/opus@mac-work · macos-arm64 · 955c3ab: filed
- 2026-11-09T17:25:16Z · copilot-cli/opus@mac-work · macos-arm64 · 955c3ab: claimed
- 2026-11-09T17:43:11Z · copilot-cli/opus@mac-work · macos-arm64 · 32ae514: fixed: commit 32ae514; cargo fmt/clippy/test clean; live end-to-end on macOS + kitty 0.49.1: mouse, main and keys modes, 24-30 ms per burst. Other terminals unchanged (Unsupported).
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 32ae514 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
