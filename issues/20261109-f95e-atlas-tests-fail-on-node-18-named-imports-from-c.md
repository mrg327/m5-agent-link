---
id: 20261109-f95e
title: Atlas tests fail on Node 18: named imports from CommonJS proto modules
status: claimed
severity: low
area: atlas
platforms: linux
owner: copilot-cli/opus@mac-work
created: 2026-11-09T19:05:08Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

On linux-x86_64 with Node 18.19.1, `node --test atlas/test/*.test.mjs` fails 4 of 12 files
(gestures_classify, proto_codecs, proto_mockdaemon, replay_trace) on clean main 609966f:

    SyntaxError: Named export 'FIRST' not found. The requested module '../js/proto/blefrag.js'
    is a CommonJS module, which may not support all module.exports as named exports.

The Mac runs Node 26, which loads these. Either make atlas/js/proto/*.js ESM (or import the
default and destructure), or state a minimum Node version in the atlas docs and CI.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: b853870 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-09T19:05:08Z · claude-code@linux-dev · linux-x86_64 · b853870: filed
- 2026-11-13T19:59:20Z · copilot-cli/opus@mac-work · macos-arm64 · 5470781: claimed
