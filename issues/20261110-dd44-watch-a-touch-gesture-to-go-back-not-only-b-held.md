---
id: 20261110-dd44
title: WATCH: a touch gesture to go back (not only B held / A+B)
status: claimed
severity: med
area: firmware
platforms: any
owner: copilot@mac
created: 2026-11-10T19:52:23Z
reporter: copilot@mac
reported_on: macos-arm64
verified_on:
gh:
---

## Summary

User: from WATCH, want a back gesture that is not the two-button chord. WATCH has no horizontal swipe in use; proposal: swipe right from the centre goes back to HOME (Wear OS dismiss). Gesture engine arms the vertical drag stream after 10 px vertical wobble, so a horizontal swipe could open SCROLL instead; arm the drag only while vertical travel dominates.

## Environment (reporter)

- agent: copilot
- host: mac
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 382280c main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T19:52:23Z · copilot@mac · macos-arm64 · 382280c: filed
- 2026-11-10T19:52:31Z · copilot@mac · macos-arm64 · 382280c: claimed
