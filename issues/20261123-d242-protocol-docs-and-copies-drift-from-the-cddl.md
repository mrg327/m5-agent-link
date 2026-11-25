---
id: 20261123-d242
title: Protocol docs and copies drift from the CDDL
status: fixed
severity: low
area: protocol
platforms: any
owner: claude-code/cloud@cloud
created: 2026-11-23T00:11:34Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

Found while writing docs/conventions/protocol.md (issue 20261122-db53). All values match across CDDL, C, Rust and JS; these are doc and coverage drift:
- CDDL header and PROTOCOL.md §2 say receivers ignore unknown keys. The device's zcbor decoder rejects unknown keys and enum values; compatibility rests on the daemon gating on the device's proto minor. Fix the docs (protocol.md PROTO-15/20 state the real rule).
- C `link_op_t` has no `cmd_op_quiet` (9), so the watch never sends it. Decide whether the watch needs it or document it as daemon-side only.
- atlas/js hello/welcome builders send proto [0, 1]; PROTOCOL.md hello example says [0, 20] and names `agentlink-cli decode` (it is `agentlink decode`); CDDL header says v0.1; "64+ are debug (DESIGN §0)" points at a section that does not say so.
- 9 of 37 message types have no vector: gone, cfg, batt, pong, log, dbg_ui_state_req, dbg_screenshot_req, dbg_screenshot_chunk, dbg_set_log_level.
- The zcbor codec is not regenerated and diffed in CI (it regenerates identically with zcbor 0.9.1 today).
- `pointer_btn` / `scroll_unit` values lack their enum prefix; `fw_status` and `result_err` are bare numbers in C; the C proto minor is a literal 24.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 76170d3+dirty conventions
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-23T00:11:34Z · claude-code@linux-dev · linux-x86_64 · 76170d3+dirty: filed
- 2026-11-25T04:45:43Z · claude-code/cloud@cloud · linux-x86_64 · 00d20be: claimed
- 2026-11-25T04:56:32Z · claude-code/cloud@cloud · linux-x86_64 · da13dc0: fixed: commit da13dc0; scripts/check format structure docs lint protocol atlas daemon sim firmware all PASS (ctest 245/245, atlas 248, firmware build, check_dram 184817 B free). Left for the phase-2 generator: pointer_btn/scroll_unit prefixes (PROTO-2), short C prefixes (PROTO-8).
    - agent: claude-code/cloud
    - host: cloud
    - platform: linux-x86_64
    - os: Ubuntu 24.04.5 LTS (kernel 6.18.44-fc-v880)
    - git: da13dc0 claude/current-overview-status-3her9f
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.5, python 3.13.18, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
