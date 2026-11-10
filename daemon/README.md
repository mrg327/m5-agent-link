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
| `crates/agentlink-hook` | Tiny shim for agent hooks: `agentlink-hook <agent> <event>` with the hook JSON on stdin. Fail-open, hard 1.5 s deadline, always exits 0, silent on stdout. Two Claude Code hooks block instead: `PermissionRequest` and `PreToolUse` for `AskUserQuestion` wait `AGENTLINK_WAIT_MS` (15 s) for a watch answer and print it in Claude Code's hook-output format; no answer prints nothing, so the terminal prompt appears as usual. Copilot hooks never block: the daemon answers its prompts by typing into the terminal instead. |
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

Environment: `AGENTLINK_HOME` (default `~/.agentlink`), `AGENTLINK_WAIT_MS` (how long a blocking hook holds the prompt for the watch, default 15000, max 120000), `AGENTLINK_WS_ADDR` (default `127.0.0.1:7878`), `AGENTLINK_USB=0` / `AGENTLINK_BLE=0` (disable a transport), `AGENTLINK_USB_PORT`, `AGENTLINK_BLE_NAME`, `AGENTLINK_BLE_WRITE_LEN` (see below), `AGENTLINK_PIPE` (Windows hook pipe name; default `\\.\pipe\agentlink-<USERNAME>-<fnv64 of socket path>`), `AGENTLINK_REMOTE` (hook: `tcp:<loopback>:<port>`, see docs/design/REMOTE.md), `AGENTLINK_SSH`, `RUST_LOG`.

BLE write size: each write-without-response carries ATT MTU − 3 bytes (fragment header included), read per frame from btleplug's negotiated MTU (CoreBluetooth: `maximumWriteValueLength(.withoutResponse)` + 3, sampled after service discovery; BlueZ: the characteristic's `MTU`; WinRT: `GattSession.MaxPduSize`). It is clamped to 20…244 B: 244 matches the firmware's MTU of 247, and a backend that has not seen the MTU exchange reports 23, which gives the old 20 B. If a larger write fails, the frame is resent at 20 B and the link stays at 20 B. Older firmware is unaffected: fragments are reassembled by header, not size, and every firmware build negotiates MTU 247. `AGENTLINK_BLE_WRITE_LEN=<2..244>` pins the size and disables the fallback. With MTU 247, a 1.5 KB frame takes 7 writes instead of 79. `RUST_LOG=agentlinkd=debug` logs `ble tx N B in K writes of <= C B` per frame and `ble rx N B in K notifications`.

## Files written

- `~/.agentlink/trace/YYYY-MM-DD.jsonl`: `{ts_us, ts, src, dir, kind, sid?, rid?, msg, bytes?}` for every hook event, registry transition and protocol message (decoded JSON plus raw hex).
- `~/.agentlink/audit.jsonl`: append-only record of focus actions and approval decisions.

## Status

Hook IPC is a Unix socket, or a named pipe on Windows. Remote hooks use `remote.sock` or a per-window `remote-<id>.sock` and must authenticate with `remote.token` first (docs/design/REMOTE.md). `cmd approve|deny|reply` for a `rid` the broker no longer holds is acked as `stale`, so a race against the terminal is safe; a device-side approve needs `hold_ms >= 1000`. Focus drivers and their verification status: DESIGN §10.

`cmd ack` sets `seen` (an `error` session then rests as `idle`), `snooze` sets `snz` to now + the arg in minutes (default 30, max 1440, 0 clears) and `notify_done` arms `ntf` (arg 0 disarms); each broadcasts a delta, an unknown `sid` is `stale`. Entering `done`/`error` sets `seen=false` and disarms `ntf`. Step summary: `pretooluse` sets `sum` (`<tool> · <command's first line | file's last two path components | url | pattern>`, ≤ 80 chars) and bumps `n`; `posttooluse*` sets `ok`; a new prompt resets `n`. Devices below proto 0.3 never get `snz`/`ntf`/`n`/`ok`; devices whose privacy is `meta` never get `sum`, and `sum` is redacted in the trace and never in the audit log.

Delta coalescing: each link holds a session's `session_upd` for up to 100 ms and merges what follows (last value wins per key), so a fast tool's start and result reach the device as one delta. It never merges two state changes, a new step `n` into anything about a step, or a step's text into its result, so every transition and every step of the device's timeline still arrives, in order. Any other message (attention, clear, gone, result, pong, snapshot) first flushes the held deltas, so its order relative to them is unchanged. A delta left with no keys the device takes (e.g. only `n`/`ok` for proto < 0.3) is not sent.
