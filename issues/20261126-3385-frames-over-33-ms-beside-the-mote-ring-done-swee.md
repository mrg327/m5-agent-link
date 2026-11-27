---
id: 20261126-3385
title: Frames over 33 ms beside the Mote: ring done sweep, replay capsule step, ambient minute shift, request pulse
status: open
severity: low
area: firmware
platforms: any
owner:
created: 2026-11-26T20:06:41Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

Found while measuring the Mote's frame cost (BENCH.md §9, simulator model, not yet on the watch). None of
these is the Mote; each is a frame the simulator flushes that the model puts over the 33 ms period:

- The ring's done sweep repaints about 115 k px (11 areas) every other frame while it sweeps
  (fun_eggs.txt after the 100th finish; any finish of a session that owns the whole ring): predicted
  about 65 ms a frame. Invalidating only the swept stretch would make it the arc's own box.
- The replay's step where a capsule changes state repaints the whole ring, about 113 k px (12 areas),
  predicted about 60 ms: inside the 100 ms step, so no step is missed, but one slow frame.
- The ambient face's burn-in guard moves the centre (a 466 x 466 object) 1 px each minute: one full
  frame (217 k px, about 75-110 ms on the watch) a minute; a request that lands then waits for it.
- A permission's ring pulse on the attention screen: about 56 k px a level, about 10 levels a second,
  predicted about 28 ms (up to about 50 ms at the model's worst).
- Pairing with the Mote on: the "Paired" sign is shown at pair done and its entrance draws hidden under
  the passkey screen for the Mote's 600 ms flare (about 10 k px a frame, 2 areas with the flare's).

Repro: the scripts named, with `frames all` after a `px_reset` (simulator/README.md). Confirm with the bench.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
- git: 04bd147+dirty worktree-agent-ae723fe2fb2a461ca
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-26T20:06:41Z · claude-code@linux-dev · linux-x86_64 · 04bd147+dirty: filed
- 2026-11-27T01:23:48Z · claude-code/cloud@cloud · linux-x86_64 · 31de7df: Commit 31de7df (titled as the Atlas watch rebuild) also carries the sim fix: a scripted headless run starts its RTC at unix 1800000000 (08:00 UTC) instead of the host's wall clock, so time-of-day tests (Mote night look, eggs) no longer pass or fail by when they run; sim_mote_boot failed after midnight UTC before it. Its commit was refused for a long subject and the change rode along.
