# Agent guide: protocol

The wire protocol between the daemon and any device: `agentlink.cddl` (normative), test vectors and
fixtures. See [README.md](README.md) and [docs/design/PROTOCOL.md](../docs/design/PROTOCOL.md).

Follow [docs/conventions/protocol.md](../docs/conventions/protocol.md) (Python tools:
[python.md](../docs/conventions/python.md)).

## Build and test

```sh
scripts/check protocol
# = uv run protocol/tools/validate_vectors.py && uv run protocol/tools/gen_vectors.py \
#   && git diff --exit-code protocol/vectors
scripts/check daemon sim firmware atlas   # every consumer of the CDDL
```

## Where new code goes

A new message or key follows S-NEW-03 in
[structure.md](../docs/conventions/structure.md), in order: `agentlink.cddl`; a vector in
`tools/gen_vectors.py` and regenerated `vectors/`; `firmware/components/agentlink_proto/regen.sh`;
the `agentlink-proto` crate; `link_rx.c` / `link_tx.c` and `agentlinkd/src/device.rs`;
`docs/design/PROTOCOL.md`; `python3 atlas/tools/extract.py`.

## Gotchas

- **The device's zcbor decoder rejects unknown keys** and unknown enum values: it drops the whole
  message. Every new field needs a proto minor bump (PROTO-16, PROTO-17) and a daemon gate on the
  peer's minor (PROTO-20). New keys are optional (`? n =>`, PROTO-21).
- Encode map keys in ascending order (PROTO-15); the device reads maps in schema order.
- **Regenerate both** the vectors (`gen_vectors.py`) and the zcbor codec (`regen.sh`) and commit
  them with the CDDL. Never edit generated output by hand (PROTO-26).
- Message types below 64 are the product; **debug types are 64 and up** (PROTO-13).
- Never reuse or renumber a type, key or enum value (PROTO-12).
- zcbor 0.9.1 needs `cbor2<5.7`, bounded arrays and no `&( … )` enums ([README.md](README.md)).
