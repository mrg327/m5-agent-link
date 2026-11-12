---
id: 20261112-2e2b
title: macOS bring-up of the next-branch daemon (proto 0.9-0.14): gaps and plan
status: fixed
severity: med
area: daemon
platforms: macos
owner: copilot-cli/opus@mac-work
created: 2026-11-12T15:57:20Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

The watch on mac-work runs firmware 09aaf62 (hello proto [0,14], from branch next), but branches next,
jobs, perf and done-summary exist only on linux-dev. main's daemon speaks 0.8, so on macOS gauges (2777),
answer window and detail (a9d5), hush/presence (fe75), standing rules (7f85), done summary (d01e) and
jobs (2e5f) are all inactive. main itself passes fmt, clippy and cargo test on macos-arm64, and it runs
live with USB and BLE (MTU 247, 244 B writes).

Gaps that macOS has to close (each was built and tested only on linux-x86_64):
1. Branches not pushed: push next (with jobs and perf if they are not merged) from linux-dev.
2. fe75: presence at the keyboard and the keep-awake inhibitor are untested on macOS. They need an idle
   source (IOKit HIDIdleTime) and a sleep assertion (caffeinate or IOPMAssertion), plus a focused-window
   check through osascript.
3. 2e5f: the zsh shell-init snippet has never run (zsh is the default shell on macOS), and neither has
   "Run again" against a real kitty on macOS.
4. 2777, 7f85, d01e: Claude Code hooks are not installed on mac-work (~/.claude/settings.json has none),
   and the statusline shim is not configured. Install both from `agentlink hooks print`, which on next
   adds PostToolUseFailure and StopFailure.
5. No launchd agent: the daemon is started by hand. A LaunchAgent would need its own Bluetooth TCC grant.
6. CoreBluetooth logged a long "Adapter powered off" loop on 2026-11-11 (sleep or BT off). The link came
   back after a restart; check that it recovers without one.

Plan: fetch next into a worktree; run the daemon checks and the e2e scripts (presence, jobs, gauges,
answer_window, standing_rules) on macos-arm64; cargo install; restart agentlinkd; install the Claude Code
hooks and the statusline; then verify items 2, 3 and 6 live.

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 9b723e7 main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-12T15:57:20Z · copilot-cli/opus@mac-work · macos-arm64 · 9b723e7: filed
- 2026-11-12T20:52:11Z · copilot-cli/opus@mac-work · macos-arm64 · 24fd401: Correction to item 4: mac-work runs Copilot CLI, so its hooks are ~/.copilot/hooks/agentlink.json. They are installed and match main's 'hooks print' (8 events), and live events reach the daemon. The Claude Code hooks matter only if Claude Code is used here; gauges (statusline), standing rules and the done summary come from Claude-only data. Copilot CLI 1.0.106 has hook events that agentlink does not register: postToolUseFailure (hook_source already handles it, so it is cheap to add), subagentStart/subagentStop and preCompact. It also has a blocking permissionRequest (allow/deny). permissionRequest fires before rules and session approvals, so blocking on it would hold even auto-allowed tools; it would need a fast pass-through before it could replace typing into kitty.
- 2026-11-12T20:54:29Z · copilot-cli/opus@mac-work · macos-arm64 · 35f8387: Copilot CLI 1.0.106 has data sources for the Claude-only features. (a) statusLine.command in ~/.copilot/settings.json gets the session status as JSON on stdin, the same mechanism as Claude Code's status line, so the agentlink-hook statusline shim from 2777 could take a copilot mode. The stdin shape is not captured yet. (b) ~/.copilot/session-state/<id>/events.jsonl: session.usage_record (model, tokens, maxPromptTokens, cost), session.usage_checkpoint (premium requests), session.mode_changed, session.model_change, session.permissions_changed, and session.shutdown codeChanges (linesAdded, linesRemoved, filesModified). (c) OpenTelemetry (COPILOT_OTEL_FILE_EXPORTER_PATH or an OTLP endpoint). Standing rules: copilot config allowedUrls and the permission settings are possible targets, but there is no Claude-style suggestion to echo back.
- 2026-11-12T22:13:39Z · copilot-cli/opus@mac-work · macos-arm64 · 65b72db: claimed
- 2026-11-12T22:22:43Z · copilot-cli/opus@mac-work · macos-arm64 · 0e9cfda: main (65b72db, next merged) did not build on macOS: presence.rs called the Linux-only locked(); clippy failed on Linux-only helpers. Fixed in 0e9cfda, along with: zsh zshexit firing in (exit N) subshells (reported the parent shell as exited), audit.jsonl lines interleaving under concurrent audits (writeln! on an unbuffered File), and shell_init tests racing a fixed 400 ms sleep. macOS presence focus now asks kitty. Checks on macos-arm64: fmt, clippy -D warnings, cargo test --workspace all pass; e2e answer_window, gauges, jobs, presence (systemd-inhibit SKIP), standing_rules, summary all pass. Installed with cargo install and restarted agentlinkd; the watch (fw 09aaf62, already current with main) links over USB and BLE with hello proto [0,14], so no flash. Still open: items 2 (live presence/keep-awake check), 3 (zsh shell-init in the user's .zshrc), 5 (launchd agent), 6 (BLE recovery after adapter off).
- 2026-11-12T22:39:09Z · copilot-cli/opus@mac-work · macos-arm64 · 4143bb0: Items 2, 3, 5 and 6 verified live on macos-arm64 with 4143bb0. (2) Presence: hid-idle reading, screen lock read as unlocked via ioreg, focus via kitty (new macOS path), keep-awake held via caffeinate -i -w <daemon pid> (PreventUserIdleSystemSleep in pmset -g assertions) and released on battery under the default ac setting. (3) eval of shell-init zsh added to ~/.zshrc. In a real kitty tab running zsh, 'sleep 12; (exit 7)' surfaced, ended as error with exit 7, and Run again (hold 1250) retyped it and it ran and failed again; shell exit was reported once. (5) daemon/tools/launchd.sh installs the LaunchAgent dev.agentlink.agentlinkd. build.rs embeds an Info.plist with NSBluetoothAlwaysUsageDescription, so CoreBluetooth works under launchd; BLE linked in 10 s and KeepAlive restarted a killed daemon. (6) blueutil off 15-20 s, then on: BLE relinked with no restart in 6-13 s over 3 debug rounds and 74 s in one earlier info-level round, with no 'Adapter powered off' loop. Item 4 is split into 20261112-c629.
- 2026-11-12T22:39:09Z · copilot-cli/opus@mac-work · macos-arm64 · 4143bb0: fixed: commits 0e9cfda, 4143bb0; checks: cargo fmt --check, clippy --all-targets -D warnings, cargo test --workspace, all 6 daemon e2e scripts, live checks for items 2/3/5/6 on macos-arm64
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: 4143bb0 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
