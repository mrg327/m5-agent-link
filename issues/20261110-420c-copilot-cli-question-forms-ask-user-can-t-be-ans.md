---
id: 20261110-420c
title: Copilot CLI question forms (ask_user) can't be answered from the watch
status: open
severity: med
area: daemon
platforms: any
owner:
created: 2026-11-10T22:12:52Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

When Copilot CLI asks through its form tool (the hook sends Notification "Information requested"), the watch shows a question and plays the input sound, but the user can't answer it from the watch. The form's fields and choices aren't in the hook payload, and typing a reply doesn't fill a structured form. Look into exposing the choices (an enum/boolean field as options) or at least an "Open on PC" path that's clearly the only action. Seen during the f718 sound test.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: fe042e2 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T22:12:52Z · copilot-cli/opus@mac-work · macos-arm64 · fe042e2: filed
