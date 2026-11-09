---
id: 20261109-59ba
title: AGENT actions audit and the Watch monitor screen
status: fixed
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-09T21:43:03Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

Audit of the AGENT screen actions found:
- Focus + acknowledge and Acknowledge overlapped, and seen was kept only on the device.
- Dismiss on an Error session did nothing.
- The daemon rejected Snooze and Notify-when-done as unsupported.
- Watch only opened STATUS.
- The daemon never filled `sum`, so the watch could not show what an agent is doing.

Plan (proto 0.3: session snz/ntf/n/ok):
- The daemon stores seen, ack dismisses errors, and snooze and notify_done are implemented.
- `sum` becomes "tool · arg" when privacy allows it; n and ok count the steps in a turn.
- Firmware: items Focus / Mark seen / Dismiss / Scroll / Snooze|Unsnooze / Notify|Cancel notify / Watch / Status...
- Watch is a live monitor screen: current step, a timeline of the last steps, steps and elapsed time; buzz on done or error.
- Snoozed sessions are muted and dimmed on every device.
Areas: daemon, firmware, protocol, docs.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: d211c22 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-09T21:43:03Z · copilot-cli/opus@mac-work · macos-arm64 · d211c22: filed
- 2026-11-09T22:14:46Z · copilot-cli/opus@mac-work · macos-arm64 · ad4acfd: fixed: Contract d211c22 (proto 0.3), daemon fdad403, firmware and docs in the following commits. Checks: cargo fmt, clippy and tests clean; ctest 53/53; idf build ok; atlas check ok. Live: headless sim against the installed daemon with replayed Copilot hooks showed the step timeline, the Done buzz on the watched session and 'Session ended'. Not yet flashed to the watch.
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: ad4acfd main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
