---
id: 20261111-2e5f
title: Jobs: long-running terminal commands on the watch (runtime, ETA, exit, stop, run again)
status: claimed
severity: med
area: daemon
platforms: any
owner: claude-code@linux-dev
created: 2026-11-11T20:24:48Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

The watch only knows about agents, because only agents have hooks. A build, a test run or a deploy started by
hand in a terminal is invisible: nothing shows that it runs, for how long, or how it ended.

Proposal (branch `jobs`, its own worktree): shell integration (`eval "$(agentlink shell-init zsh)"`, bash too)
reports every command's start and end to the daemon. A command still running after 10 s becomes a session
with agent `shell`: the card shows the command, the elapsed time and, when the same command has run before in
that directory, an estimate from the median of its earlier runs. When it ends the watch buzzes and shows
Finished or Failed with the exit status and the run time. From the watch: Stop (hold; SIGINT to the terminal's
foreground process group), Run again (hold; typed into the same terminal while its shell is idle), Dismiss.
Protocol 0.9: `agent_shell`, session keys `eta` and `exit`, `cmd_op_rerun`. Interactive programs and agents
are never shown. Command text is privacy-gated like `sum` and never written to the trace or the history file.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: 3ae24c0 HEAD
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T20:24:48Z · claude-code@linux-dev · linux-x86_64 · 3ae24c0: filed
- 2026-11-11T20:24:48Z · claude-code@linux-dev · linux-x86_64 · 3ae24c0: claimed
- 2026-11-11T20:53:48Z · claude-code@linux-dev · linux-x86_64 · 670f4a8: Built on branch jobs (own worktree), 3 commits: device side + protocol 0.9, daemon + shell integration + CLI, spec docs/design/JOBS.md. Checks on linux-x86_64: ctest 97/97, atlas 146/146, cargo test 208 pass (keys gate flaky under load, issue 5617), clippy/fmt clean, vectors 42/42, firmware builds with check_dram 127281 B free, daemon/tools/jobs_e2e.py 18/18 against a real bash (it found and now guards an out-of-order start/end bug). Not run: zsh snippet (no zsh here), macOS paths, Run again against a real kitty, anything on the glass. Not flashed, not pushed, not merged.
