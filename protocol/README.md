# protocol/

The wire protocol between the PC daemon and any agent-link device.

| File | Purpose |
|---|---|
| `agentlink.cddl` | **Normative schema** (CBOR, integer keys, discriminated `envelope`). |
| `vectors/*.json` | Shared test vectors: symbolic JSON + `cbor_hex`. Every implementation must round-trip them. |
| `tools/gen_vectors.py` | Regenerates `vectors/` from the symbolic definitions. |
| `tools/validate_vectors.py` | Checks every vector against the CDDL (run in CI). |

Semantics live in [`docs/design/PROTOCOL.md`](../docs/design/PROTOCOL.md).

## Commands

```sh
uv run protocol/tools/gen_vectors.py        # regenerate vectors
uv run protocol/tools/validate_vectors.py   # validate vectors against CDDL

# C codec for firmware (zcbor 0.9.1)
uvx --with 'cbor2<5.7' zcbor code -c protocol/agentlink.cddl -t envelope -d -e \
    --output-c agentlink_codec.c --output-h agentlink_codec.h
```

## zcbor constraints on the CDDL

- Pin `cbor2<5.7` (zcbor 0.9.1 breaks with newer versions).
- No `&( name: value )` enums. Write named constants (`x = x_a / x_b`, `x_a = 0`).
- Enum labels must not collide with rule names.
- Arrays must be bounded (`[0*8 foo]`, not `[* foo]`) so that the generated C has fixed storage.
- Messages are `{ 0 => mt_<type>, hdr, 3 => body }`. The type id selects the body.
