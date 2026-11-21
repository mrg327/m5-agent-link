---
id: 20261115-ea88
title: Dictation is slow on a CPU-only machine: whisper encodes 30 s for every recording
status: claimed
severity: med
area: daemon
platforms: any
owner: claude-code@linux-dev
created: 2026-11-15T23:13:24Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

On a 2-core laptop without a usable GPU a 4 s dictation took 5.7 s to transcribe, 11 s took 6.3 s and 23 s took 7.9 s (whisper.cpp, base.en, 4 threads). About 4.3 s of each is the encoder, which processes a 30 s window whatever the recording's length; the rest is beam-5 decoding.

Measured (min of 3, clips of 4 / 11 / 23 s; errors are words wrong against the known text):
- now, full window, beam 5: 5.67 / 6.22 / 7.90 s, 0 errors
- -ac sized to the recording + 128 frames, beam 5: 1.52 / 3.41 / 6.78 s, 0 errors
- the same, beam 2: 1.35 / 2.77 / 5.29 s, 0 errors
- the same, greedy: 1.24 / 2.43 / 4.61 s, 0 / 1 / 4 errors
- -ac with 64 frames of padding: 5 errors on the short clip
- tiny.en, -ac +128, greedy: 0.65 / 1.23 / 2.27 s and 0 errors on clean clips, but on clips cut mid-word it took 5 to 8 s and garbled two of six; base.en stayed at 1.4 to 2.6 s and correct
- 2 threads instead of 4: slightly slower in every pairing

Chosen: base.en, -ac = recording + 128 frames (the full 1500 from 27 s up), beam 2.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: eadae85+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-15T23:13:25Z · claude-code@linux-dev · linux-x86_64 · eadae85+dirty: filed
- 2026-11-15T23:13:33Z · claude-code@linux-dev · linux-x86_64 · eadae85+dirty: claimed
- 2026-11-15T23:25:38Z · claude-code@linux-dev · linux-x86_64 · 1c6865c: Change in main (1c6865c): -ac sized to the recording + 128 frames, beam 2, AGENTLINK_WHISPER_BEAM to override. Daemon checks pass (fmt, clippy, 331 tests incl. real speech through the watch format). Release binary built; installing it on the Linux machine and restarting the service is left to the user (blocked by the session's permission mode), so it is not yet verified on a real dictation.
