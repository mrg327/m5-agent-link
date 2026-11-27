---
id: 20261127-dd88
title: Shell jobs in kitty bash: kitty's late prompt hook is reported as an endless job and real commands go unreported
status: fixed
severity: med
area: daemon
platforms: linux,macos
owner: claude-code@linux-dev
created: 2026-11-27T03:30:14Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

## What happens

In bash under kitty's shell integration, the watch shows a `shell` job that never ends (repo = the shell's
cwd, tool `builtin`, summary `builtin declare -F _ksi_prompt_command > /dev/null 2> /dev/null`), and the
commands actually typed are not reported at all. Seen 2026-11-26 on linux (kitty, bash 5.2).

Cause, `daemon/crates/agentlink/src/shell_init.rs` (bash snippet): `_agentlink_arm` must be the last
PROMPT_COMMAND entry, since it arms the DEBUG trap for "the next command is the typed line". kitty appends
its own entry (`kitty.bash`, `PROMPT_COMMAND+=("$pc")`) after the rc files ran, so kitty's command runs after
the arm, takes it, and is reported as a start; its end goes out at the next prompt, which starts it again.
The typed line then finds the trap disarmed. Any tool that adds a prompt hook after ours does the same.

Repro: interactive `bash --norc -i` with the snippet sourced, then
`PROMPT_COMMAND+=("builtin declare -F _k > /dev/null 2> /dev/null && _k")`, then `echo one`: the log has a
start for `builtin declare -F _k …` at every prompt and none for `echo one`.

## Fix

Arm from PS0 (bash >= 4.4: expanded after the line is read and before it runs, so no prompt hook can take
the arm), and have `_agentlink_arm` move itself back to the end of PROMPT_COMMAND when something was added
after it (older bash, or a PS0 that gets overwritten). Test with a late hook, array and string forms.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 2d3dc41 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-27T03:30:15Z · claude-code@linux-dev · linux-x86_64 · 2d3dc41: filed
- 2026-11-27T03:30:15Z · claude-code@linux-dev · linux-x86_64 · 2d3dc41: claimed
- 2026-11-27T03:34:55Z · claude-code@linux-dev · linux-x86_64 · 12f4a36: fixed: commit 12f4a36; checks format structure commits lint daemon PASS (shell_init 9/9). Verified on linux with kitty's own kitty.bash sourced after the snippet: old snippet reported _ksi_prompt_command and dropped the typed lines, fixed one reports exactly the typed lines. Not tried on macOS bash 3.2 (PS0 path off there; the move-to-end fallback covers it, untested on that shell).
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
    - git: 12f4a36 main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
