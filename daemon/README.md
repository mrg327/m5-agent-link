# daemon/

The cross-platform PC side of agentlink (Rust, tokio). It collects agent status, pushes it to devices, and records everything it does.

```
agent hooks ─► agentlink-hook ─► ~/.agentlink/hook.sock ─► agentlinkd ─► device (USB / BLE / WebSocket)
                                                              │
                                                     ~/.agentlink/trace/YYYY-MM-DD.jsonl
```

## Crates

| Crate | What |
|---|---|
| `crates/agentlink-proto` | Wire types and CBOR codec (`minicbor`, integer keys) mirroring `protocol/agentlink.cddl`; `Envelope::{to_bytes, from_bytes}`; serde JSON (symbolic names) for traces. Tests round-trip every `protocol/vectors/*.json`. |
| `crates/agentlinkd` | The daemon. `source.rs` (`AgentSource` trait), `hook_source.rs` (hook IPC → events), `registry.rs` (sessions + deltas), `transport.rs` (`Transport`/`Link` traits), `ws.rs` (WebSocket, 127.0.0.1:7878), `ble.rs` (+`ble/frag.rs`), `usb.rs` (+`usb/frame.rs`), `focus/` (`FocusDriver`; kitty, Terminal.app, Windows Terminal, tmux, generic), `broker.rs` (approval broker: `rid` → the hook waiting for a decision; first answer wins), `prompt.rs` (Copilot prompts: cards from the screen, a device answer typed into kitty; DESIGN §6a), `device.rs` (hello→welcome→snapshot, deltas, attentions, `cmd` handling), `deltas.rs` (per-link delta coalescing), `trace.rs` (JSONL trace + audit log). |
| `crates/agentlink-hook` | Tiny shim for agent hooks: `agentlink-hook <agent> <event>` with the hook JSON on stdin. Fail-open, hard 1.5 s deadline, always exits 0, silent on stdout. Two Claude Code hooks block instead: `PermissionRequest` and `PreToolUse` for `AskUserQuestion` wait for the daemon's answer window (60 s by default, docs/design/ANSWER-WINDOW.md; 15 s with an older daemon) for a watch answer and print it in Claude Code's hook-output format; no answer prints nothing, so the terminal prompt appears as usual. Copilot hooks never block: the daemon answers its prompts by typing into the terminal instead. |
| `crates/agentlink` | CLI: `status`, `trace [--follow] [--sid N]`, `decode <hex>`, `hooks print`, `remote init [--rotate]`, `remote add\|edit\|list\|rm\|setup` (saved remotes, Windows and Linux/WSL onboarding), `ssh [--port P] [--no-cmd] <name\|host> [ssh args]` (REMOTE.md §4), `device state\|tap\|press\|lift` (stubs). All accept `--json`. |

## Build, test, lint

On Linux, BLE (`btleplug`, via BlueZ/D-Bus) needs `sudo apt-get install -y libdbus-1-dev pkg-config`.

```sh
. ~/.cargo/env          # if cargo is not on PATH
cd daemon
cargo build --workspace
cargo test --workspace  # includes the 12-vector round trip and a daemon end-to-end smoke test
cargo clippy --workspace -- -D warnings
```

## Run

```sh
cargo run -p agentlinkd                      # ws://127.0.0.1:7878, hook socket ~/.agentlink/hook.sock
cargo run -p agentlink -- status
cargo run -p agentlink -- trace --follow
cargo run -p agentlink -- decode a30008010503a200000102
cargo run -p agentlink -- hooks print        # config snippets (nothing is installed)
echo '{"sessionId":"s1","toolName":"bash"}' | cargo run -p agentlink-hook -- copilot preToolUse
```

Environment: `AGENTLINK_HOME` (default `~/.agentlink`), `AGENTLINK_ANSWER_WINDOW_S` (daemon: how long a blocking hook holds the terminal prompt for the watch, default 60, 5-300, 0 = never; also `~/.agentlink/answer_window`, read per request; see docs/design/ANSWER-WINDOW.md), `AGENTLINK_PRESENCE` (daemon: `on`/`off`, optionally with the idle limit in seconds, also `~/.agentlink/presence`; off = alerts and the window as before presence; see docs/design/PRESENCE.md), `AGENTLINK_KEEP_AWAKE` (daemon: `ac` (default), `always`, `off`; whether to hold an idle-sleep inhibitor while a session works or a job runs, also `~/.agentlink/keep_awake`), `AGENTLINK_PRESENCE_FILE` (TEST ONLY: a file that stands in for the presence and power sources, see PRESENCE.md section 4), `AGENTLINK_WAIT_MS` (hook: pins that hook's window, max 300000; an older daemon's whole window, default 15000), `AGENTLINK_WS_ADDR` (default `127.0.0.1:7878`), `AGENTLINK_USB=0` / `AGENTLINK_BLE=0` (disable a transport), `AGENTLINK_USB_PORT`, `AGENTLINK_BLE_NAME`, `AGENTLINK_BLE_WRITE_LEN` (see below), `AGENTLINK_PIPE` (Windows hook pipe name; default `\\.\pipe\agentlink-<USERNAME>-<fnv64 of socket path>`), `AGENTLINK_REMOTE` (hook: `tcp:<loopback>:<port>`, see docs/design/REMOTE.md), `AGENTLINK_SSH`, `RUST_LOG`.

BLE write size: each write-without-response carries ATT MTU − 3 bytes (fragment header included), read per frame from btleplug's negotiated MTU (CoreBluetooth: `maximumWriteValueLength(.withoutResponse)` + 3, sampled after service discovery; BlueZ: the characteristic's `MTU`; WinRT: `GattSession.MaxPduSize`). It is clamped to 20…244 B: 244 matches the firmware's MTU of 247, and a backend that has not seen the MTU exchange reports 23, which gives the old 20 B. If a larger write fails, the frame is resent at 20 B and the link stays at 20 B. Older firmware is unaffected: fragments are reassembled by header, not size, and every firmware build negotiates MTU 247. `AGENTLINK_BLE_WRITE_LEN=<2..244>` pins the size and disables the fallback. With MTU 247, a 1.5 KB frame takes 7 writes instead of 79. `RUST_LOG=agentlinkd=debug` logs `ble tx N B in K writes of <= C B` per frame and `ble rx N B in K notifications`.

## Jobs

Commands typed into a terminal that are still running after 10 s get a card on the watch (docs/design/JOBS.md). Setup is one line in the shell's rc file, with `agentlink-hook` on PATH:

```sh
eval "$(agentlink shell-init zsh)"     # or bash
```

The snippet calls `agentlink-hook shell start|end|exit --pid <shell> …` in the background (the shim never reads stdin and never delays a command). `agentlinkd` tracks every command per shell (`shell:<pid>`) and shows it as a session only past the threshold, with `AGENTLINK_JOB_MIN_S` (default 10, fractions allowed). Ends: exit 0 is `done`, other is `error` with `exit`, 130 (Ctrl-C) removes the card silently. A card goes on `ack` (Dismiss or Button A), 10 minutes after the job ended, when the shell exits (also when its pid disappears, checked every 5 s) or when the same shell starts another long job. `cancel` (hold) sends SIGINT to the terminal's foreground process group; `rerun` (hold) types the command line again through the kitty typing driver.

- `~/.agentlink/jobs-ignore.txt`: extra program names never shown, one per line, `#` comments. Built in: editors, pagers, `ssh`, `tmux`, `top`, `watch`, `fg`, the agent CLIs, and REPLs or shells started without arguments. A terminal that already hosts an agent session is skipped too.
- `~/.agentlink/job-history.json`: for the ETA only. Key = hash of directory + command line, value = the last 5 successful run times in seconds (runs of 5 s or more), at most 500 keys. No command text. The ETA is the median once there are 2 runs.
- The trace and the audit log never hold the command line (`payload.cmd` of the hook line and `sum` are replaced by `<private>`; `rerun` is audited without its text). Devices below proto 0.9 get neither job sessions nor `eta` / `exit`.

## Gauges

Plan usage, context fill, model, permission mode and pull request on the watch (docs/design/GAUGES.md). The
data comes from Claude Code's status line. Print the setting and merge it into `~/.claude/settings.json`
yourself (nothing is installed; your current status-line command, if any, is read and kept):

```sh
agentlink hooks statusline                 # or: --chain '<your status-line command>'
```

`agentlink-hook statusline [-- <command>]` reads the status-line JSON, forwards `session_id`, `cwd`, `model`,
`context_window.used_percentage`, `rate_limits.five_hour|seven_day` and `pr` (nothing else) as
`event: "statusline"`, then runs `<command>` with the same stdin and passes its output and exit status
through. `usage.rs` keeps the account's two windows (local status lines only), projects when they run out at
the current pace and publishes the `usage` message; a status line updates only a session the hooks created.
`permission_mode` comes from the hook payloads. Devices below proto 0.10 get none of it; the pull request is
withheld under privacy `meta`. Try it:

```sh
echo '{"session_id":"s1","context_window":{"used_percentage":42}}' | cargo run -p agentlink-hook -- statusline
```

## Presence and keep-awake

docs/design/PRESENCE.md. The daemon asks whether the user is at the keyboard (screen saver / idle monitor over D-Bus and logind on Linux, `ioreg` on macOS; unknown elsewhere) and whether a session's terminal is the focused window (kitty remote control, X11 `xdotool`). With the terminal focused the default answer window is 2 s and the watch is told not to buzz (`hush`, proto 0.12); at the keyboard elsewhere the window is 10 s; away, locked or unknown nothing changes. An explicit `answer_window` setting is never shortened. `AGENTLINK_PRESENCE=off` / `~/.agentlink/presence` turns the rules off. While a session works or waits on the user, or a job runs, the daemon holds an idle-sleep inhibitor (`systemd-inhibit --what=idle` on Linux, `caffeinate -i` on macOS) according to `~/.agentlink/keep_awake`: `ac` (default, on AC power only), `always`, `off`. `agentlink status` prints both (`presence:`, `keep awake:`, and `--json`). End to end: `uv run --with cbor2 --with websockets python daemon/tools/presence_e2e.py "$PWD/.."`.

## Files written

- `~/.agentlink/trace/YYYY-MM-DD.jsonl`: `{ts_us, ts, src, dir, kind, sid?, rid?, msg, bytes?}` for every hook event, registry transition and protocol message (decoded JSON plus raw hex).
- `~/.agentlink/audit.jsonl`: append-only record of focus actions and approval decisions.

## Status

Hook IPC is a Unix socket, or a named pipe on Windows. Remote hooks use `remote.sock` or a per-window `remote-<id>.sock` and must authenticate with `remote.token` first (docs/design/REMOTE.md). `cmd approve|deny|reply` for a `rid` the broker no longer holds is acked as `stale`, so a race against the terminal is safe; a device-side approve needs `hold_ms >= 1000`. Focus drivers and their verification status: DESIGN §10.

`cmd ack` sets `seen` (an `error` session then rests as `idle`), `snooze` sets `snz` to now + the arg in minutes (default 30, max 1440, 0 clears) and `notify_done` arms `ntf` (arg 0 disarms); each broadcasts a delta, an unknown `sid` is `stale`. Entering `done`/`error` sets `seen=false` and disarms `ntf`. Step summary: `pretooluse` sets `sum` (`<tool> · <command's first line | file's last two path components | url | pattern>`, ≤ 80 chars) and bumps `n`; `posttooluse*` sets `ok`; a new prompt resets `n`. Devices below proto 0.3 never get `snz`/`ntf`/`n`/`ok`; devices whose privacy is `meta` never get `sum`, and `sum` is redacted in the trace and never in the audit log.

Delta coalescing: each link holds a session's `session_upd` for up to 100 ms and merges what follows (last value wins per key), so a fast tool's start and result reach the device as one delta. It never merges two state changes, a new step `n` into anything about a step, or a step's text into its result, so every transition and every step of the device's timeline still arrives, in order. Any other message (attention, clear, gone, result, pong, snapshot) first flushes the held deltas, so its order relative to them is unchanged. A delta left with no keys the device takes (e.g. only `n`/`ok` for proto < 0.3) is not sent.

Done summary (docs/design/DONE-SUMMARY.md, proto 0.14): the daemon tallies each Claude Code turn's edited files, lines, last test verdict and length from the hook payloads and sends them with the delta that enters done / error. End to end: `uv run --with cbor2 --with websockets python daemon/tools/summary_e2e.py "$PWD/.."`.

The printed Claude Code hooks (`agentlink hooks print`) include `PostToolUseFailure` and `StopFailure`, which the done summary needs (failed test runs, errored turns); re-merge the block into `~/.claude/settings.json` after upgrading.
