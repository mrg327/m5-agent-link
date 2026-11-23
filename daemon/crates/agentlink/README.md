# agentlink (CLI)

The command-line client: `status`, `trace`, `decode`, `hooks`, `remote`, `ssh`, `ota`, `power`,
`graph`, `bench` and more. It talks to `agentlinkd` over its IPC socket. Full command list:
[daemon/README.md](../../README.md).

## Build and test

```sh
scripts/check daemon                     # fmt, clippy -D warnings, cargo test --workspace
cargo run -p agentlink -- status         # from daemon/; every command accepts --json
```

| File | Concern |
|---|---|
| `main.rs` | Argument parsing (`clap`) and dispatch |
| `status.rs`, `trace.rs` | Daemon state and the JSONL trace |
| `hooks.rs`, `shell_init.rs` | Print and install agent hooks; embeds `daemon/integrations/pi/agentlink.ts` |
| `remote.rs`, `remotes.rs`, `ssh.rs` | Remote sessions over SSH ([REMOTE.md](../../../docs/design/REMOTE.md)) |
| `ota.rs`, `power.rs`, `graph.rs`, `bench.rs` | One feature each; see its design doc |

## Rules that bind it

- S-CRATE-02: a binary that depends on `agentlink-proto` only, never on `agentlinkd`.
- RS-26: no `unsafe`.
- RS-16: `anyhow::Result` at the edges.
