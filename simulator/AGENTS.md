# Agent guide: simulator

The firmware `core` on SDL2: a host HAL, a script runner and the unit tests. See
[README.md](README.md) and [docs/design/FIRMWARE.md](../docs/design/FIRMWARE.md).

Follow [docs/conventions/c.md](../docs/conventions/c.md). The host HAL and the white-box harness
have their own rules in [structure.md](../docs/conventions/structure.md) (S-DEP-07, S-DEP-08).

## Build and test

```sh
scripts/check sim
# = cmake -S simulator -B build/sim -G Ninja && cmake --build build/sim \
#   && ctest --test-dir build/sim -j 16 --output-on-failure
ctest --test-dir build/sim -R sim_task_approve --output-on-failure   # one test
build/sim/agentlink_sim --no-ws --headless --script simulator/scripts/selftest.txt
```

## Where new code goes

- A unit test for a pure core module: `tests/test_<name>.c` plus an `add_test` line in
  `CMakeLists.txt`.
- A scripted UI run: `scripts/<name>_*.txt` with `shot` and `expect_*` lines, plus a
  `sim_script_test` entry.
- A new HAL operation: `src/sim_*.c`, in the same commit as `hal.h` (S-NEW-02).
- Reach-ins to `core/src` private headers stay in the script runner or a test, never in the host
  HAL (S-DEP-08).

## Gotchas

- **Script order matters.** In a `--no-ws` script, `connect` comes before an `rx` welcome line;
  `connect` is what fakes the transport being up.
- **The simulator runs at desktop speed.** Its link drains in under a millisecond, so it hides
  timing bugs (UX-PROFILE §1). Timing numbers come from the bench on the watch
  ([BENCH.md](../docs/design/BENCH.md)), not from `cpu_us` here.
- It cannot catch internal-RAM limits either: the device build and its boot log do
  ([firmware/AGENTS.md](../firmware/AGENTS.md)).
- Scripts replay in real time, so run ctest with `-j 16`.
- `atlas/wasm/lv_conf.h` is a copy of `lv_conf.h`; change both.
