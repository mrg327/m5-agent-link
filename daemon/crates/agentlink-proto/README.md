# agentlink-proto

The wire types and CBOR codec (`minicbor`, integer keys) on the daemon side. They mirror the
normative [`protocol/agentlink.cddl`](../../../protocol/agentlink.cddl) field for field, and serialise
to JSON with the symbolic names the trace and `agentlink decode` print. Semantics:
[PROTOCOL.md](../../../docs/design/PROTOCOL.md).

## Build and test

```sh
scripts/check daemon protocol            # the crate, then the vectors it must round-trip
cargo test -p agentlink-proto            # from daemon/: tests/vectors.rs reads protocol/vectors
```

| File | Concern |
|---|---|
| `src/lib.rs` | The `PROTO` version and the public API |
| `src/envelope.rs` | `Envelope::{to_bytes, from_bytes}`, `Message` (unknown types kept as `Unknown`) |
| `src/body.rs`, `src/enums.rs` | Message bodies and enums, in CDDL key order |
| `src/battery.rs` | The LiPo voltage curve and the power log's columns ([POWER.md](../../../docs/design/POWER.md)) |
| `tests/vectors.rs` | Round trip of every `protocol/vectors/*.json` |

## Rules that bind it

- S-CRATE-01 / RS-5: depends on no workspace crate. RS-26: no `unsafe`.
- PROTO-15: fields are declared in key order, so they encode in ascending key order.
- PROTO-17: a minor bump changes `PROTO` here together with the CDDL, `link_tx.c` and the gate in
  `agentlinkd/src/device.rs` ([protocol.md](../../../docs/conventions/protocol.md)).
- Hand-kept mirror of the CDDL; the vectors hold the two together (structure.md S-GEN table).
