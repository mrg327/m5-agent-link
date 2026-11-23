# agentlinkd

The daemon: it takes agent hook events over a local socket, keeps the session registry, and talks
to devices over USB, BLE and WebSocket. Overview and environment variables:
[daemon/README.md](../../README.md); design: [DESIGN.md](../../../docs/design/DESIGN.md).

## Build and test

```sh
scripts/check daemon                      # fmt, clippy -D warnings, cargo test --workspace
cargo test -p agentlinkd                  # from daemon/: unit tests and tests/smoke.rs
cargo run -p agentlinkd                   # ws://127.0.0.1:7878, hook socket under AGENTLINK_HOME
```

End-to-end tests drive the real binary from [`daemon/tools/`](../../tools/README.md).

| File | Concern |
|---|---|
| `main.rs` | Start-up, module list, the event loop |
| `device.rs` | Per-link handshake (hello, welcome, snapshot), deltas, attentions, `cmd` handling, the `*_MINOR` gates |
| `registry.rs`, `deltas.rs`, `turn.rs` | Sessions, per-link delta coalescing, per-turn tallies |
| `transport.rs`, `ws.rs`, `ble.rs` (+`ble/`), `usb.rs` (+`usb/`) | The `Transport` / `Link` traits and the three transports |
| `hook_source.rs`, `source.rs`, `ipc.rs`, `remote.rs` | Hook IPC (Unix socket, Windows pipe), remote hooks |
| `broker.rs`, `ask.rs`, `standing.rs`, `prompt.rs` | Approvals and questions: who waits for which answer |
| `focus/` | Focus drivers: kitty, Terminal.app, Windows Terminal, tmux, generic |
| `jobs.rs`, `usage.rs`, `lastmsg.rs`, `graph.rs`, `presence.rs`, `keepawake.rs`, `power.rs`, `ota.rs`, `bench.rs`, `voice.rs`, `keys.rs` | One feature each, named after its design doc |
| `trace.rs` | JSONL trace and audit log |
| `build.rs` | Embeds an Info.plist on macOS (Bluetooth usage string for launchd) |

## Rules that bind it

- S-CRATE-02: depends on library crates only; the CLI reaches it over IPC.
- S-NEW-04: a feature is one module here plus its CLI side, an e2e test and a README section.
- RS-27: `unsafe` only in OS-binding modules, each block with a `// SAFETY:` comment (RS-28).
- PROTO-20: never send a device what its proto minor does not know.
