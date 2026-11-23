# Agent guide: daemon

The Rust workspace on the PC: `agentlinkd`, the `agentlink` CLI, the hook shim and three library
crates, plus `integrations/` and the end-to-end `tools/`. See [README.md](README.md) and
[docs/design/DESIGN.md](../docs/design/DESIGN.md).

Follow [docs/conventions/rust.md](../docs/conventions/rust.md) (TS in `integrations/`:
[js.md](../docs/conventions/js.md); Python in `tools/`: [python.md](../docs/conventions/python.md)).

## Build and test

```sh
scripts/check daemon
# = (cd daemon && cargo fmt --check && cargo clippy --workspace -- -D warnings && cargo test --workspace)
#   && node --test daemon/integrations/pi/      (Node 24)
uv run --with cbor2 --with websockets python daemon/tools/<feature>_e2e.py "$PWD"   # from the repo root
```

A change in `crates/agentlinkd/src` also needs `scripts/check atlas`.

## Where new code goes

- A feature: `crates/agentlinkd/src/<feature>.rs`, its CLI side in `crates/agentlink/src/<feature>.rs`,
  an end-to-end test in `tools/<feature>_e2e.py`, a section in `README.md` (S-NEW-04).
- A new library crate only for logic with no daemon state, no async and no IO.

## Rules that break most often here

- S-CRATE-01 / RS-5: `agentlink-proto`, `agentlink-keys`, `agentlink-delta` use no workspace crate.
- S-CRATE-03: `agentlink-hook` stays tiny and fail-open, and depends on no workspace crate.
- RS-19: no `unwrap()` or `expect()` outside tests.

## Gotchas

- **Never `pkill -x agentlinkd`** on a machine that runs the daemon as a service: it kills the
  service too. Stop a hand-run daemon by its PID.
- **Use a short `AGENTLINK_HOME` for a hand-run daemon** (a symlink such as `/tmp/alb`): Unix
  socket paths are limited to about 100 bytes.
- **A second daemon beside the service** runs on USB only, with its own home and its own WS port
  (`AGENTLINK_USB=1 AGENTLINK_BLE=0 AGENTLINK_WS_ADDR=127.0.0.1:7979`, BENCH.md §7).
- **Protocol changes are gated on the device's proto minor.** The watch's decoder drops a message
  with a key it does not know, so strip it for older peers with a `<FEATURE>_MINOR` gate in
  `device.rs` ([protocol.md](../docs/conventions/protocol.md) PROTO-19, PROTO-20).
- Each e2e script starts its own daemon with a scratch home and leaves the user's daemon alone.
- A hand-run daemon on USB blocks `idf.py flash`; stop it before flashing.
