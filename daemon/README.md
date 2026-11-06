# daemon/

The cross-platform PC side of agentlink (Rust, tokio). It collects agent status, pushes it to devices, and records everything it does.

```
agent hooks ─► agentlink-hook ─► ~/.agentlink/hook.sock ─► agentlinkd ─► device (WebSocket now; BLE/USB later)
                                                              │
                                                     ~/.agentlink/trace/YYYY-MM-DD.jsonl
```

## Crates

| Crate | What |
|---|---|
| `crates/agentlink-proto` | Wire types and CBOR codec (`minicbor`, integer keys) mirroring `protocol/agentlink.cddl`; `Envelope::{to_bytes, from_bytes}`; serde JSON (symbolic names) for traces. Tests round-trip every `protocol/vectors/*.json`. |
| `crates/agentlinkd` | The daemon. `source.rs` (`AgentSource` trait), `hook_source.rs` (hook IPC → events), `registry.rs` (sessions + deltas), `transport.rs` (`Transport`/`Link` traits), `ws.rs` (WebSocket, 127.0.0.1:7878), `ble.rs`/`usb.rs` (TODO stubs), `focus.rs` (`FocusDriver`, logging no-op), `device.rs` (hello→welcome→snapshot, deltas, `cmd` handling), `trace.rs` (JSONL trace + audit log). |
| `crates/agentlink-hook` | Tiny shim for agent hooks: `agentlink-hook <agent> <event>` with the hook JSON on stdin. Fail-open, hard 1.5 s deadline, always exits 0, silent on stdout. |
| `crates/agentlink` | CLI: `status`, `trace [--follow] [--sid N]`, `decode <hex>`, `hooks print`, `device state\|tap\|press\|lift` (stubs). All accept `--json`. |

## Build, test, lint

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

Environment: `AGENTLINK_HOME` (default `~/.agentlink`), `AGENTLINK_WS_ADDR` (default `127.0.0.1:7878`), `RUST_LOG`.

## Files written

- `~/.agentlink/trace/YYYY-MM-DD.jsonl`: `{ts_us, ts, src, dir, kind, sid?, rid?, msg, bytes?}` for every hook event, registry transition and protocol message (decoded JSON plus raw hex).
- `~/.agentlink/audit.jsonl`: append-only record of focus actions and approval decisions.

## Status

Scaffold. Hook IPC is Unix-only (Windows named pipe is a stub). `cmd approve|deny` is acked as `stale` until the approval broker exists; `cmd focus` calls the no-op focus driver.
