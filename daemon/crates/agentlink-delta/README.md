# agentlink-delta

Delta firmware updates: a detools sequential patch (bsdiff, heatshrink-compressed) in the format the
watch's vendored detools decoder applies. `agentlinkd` uses it for OTA
([PROTOCOL.md](../../../docs/design/PROTOCOL.md) §6c, [FIRMWARE.md](../../../docs/design/FIRMWARE.md) §9).

## Build and test

```sh
scripts/check daemon                     # fmt, clippy -D warnings, cargo test --workspace
cargo test -p agentlink-delta            # from daemon/: compares against protocol/fixtures/ota
cargo run --release -p agentlink-delta --example delta -- create BASE NEW PATCH
```

| Path | Concern |
|---|---|
| `src/lib.rs` | Patch layout, `WINDOW_SZ2` / `LOOKAHEAD_SZ2`, create and apply |
| `src/bsdiff.rs`, `src/heatshrink.rs` | The diff and the compressor |
| `examples/delta.rs` | Make or apply a patch from the command line, with timing |
| `THIRD_PARTY.md` | Licences of the algorithms it ports |

## Rules that bind it

- S-CRATE-01 / RS-5: depends on no workspace crate. RS-26: no `unsafe`.
- Patches are byte-identical to detools 0.53.1 with heatshrink 11/10; the test checks
  `protocol/fixtures/ota/rust.patch`, regenerated with `uv run protocol/fixtures/ota/gen.py`.
