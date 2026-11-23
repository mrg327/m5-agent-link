# Agent guide: bench

Task scripts the bench plays on the real watch, as a finger, to measure what the user feels. See
[README.md](README.md) and [docs/design/BENCH.md](../docs/design/BENCH.md).

Scripts use the simulator script language ([simulator/README.md](../simulator/README.md)).
Bench code lives in `firmware/components/core/src/bench.c`, `daemon/crates/agentlinkd/src/bench.rs`
and `daemon/crates/agentlink/src/bench.rs`; follow [c.md](../docs/conventions/c.md) and
[rust.md](../docs/conventions/rust.md) there.

## Build and test

```sh
scripts/check sim                                         # each task also runs as ctest sim_task_<name>
ctest --test-dir build/sim -R sim_task_ --output-on-failure
agentlink bench run bench/tasks/*.txt --runs 3 [--human 1]   # on the watch, through the daemon
```

## Where new code goes

A new task is `tasks/<name>.txt` (S-NEW-06), between `bench start` and `bench stop` / `bench save`.
Register it in `simulator/CMakeLists.txt`: `bench_task_test(<name> "<command regex>")` when it ends
with a command, or the `foreach` list of screen-only tasks whose `expect_screen` lines are the check.

## Gotchas

- **Task scripts carry a proto 0.24 welcome** (the `rx` line after `connect`), so the simulator's
  menus match the watch's.
- **Linking can take up to 30 s.** A watch welcomed on BLE offers a hello on USB every 5 s and backs
  off to 30 s while the offer does not go out (`USB_PREFER_*` in `link.c`).
- **Strokes wake the display like a finger**: the first touch on a dark display only wakes it. Do not
  script around it (UX-PROFILE §1).
- To try a daemon build beside the service: USB only, its own short `AGENTLINK_HOME`, its own WS
  port (BENCH.md §7). Stop it by its PID, never with `pkill -x agentlinkd`.
- Numbers come from the watch. The simulator runs the same scripts at desktop speed and only checks
  that each task works.
