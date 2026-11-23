# Agent guide: firmware

The ESP-IDF project for the watch (M5Stack StopWatch): portable `components/core`, the `hal.h`
contract, the zcbor codec, the board and `main/`. See [README.md](README.md) and
[docs/design/FIRMWARE.md](../docs/design/FIRMWARE.md).

Follow [docs/conventions/c.md](../docs/conventions/c.md) and the layering rules in
[structure.md](../docs/conventions/structure.md) (S-DEP-01 to 10, S-CORE-01 to 05).

## Build and test

```sh
scripts/check sim         # every core change: cmake -S simulator -B build/sim -G Ninja && cmake --build build/sim && ctest --test-dir build/sim -j 16 --output-on-failure
scripts/check firmware    # device build: idf.py -C firmware build && python3 firmware/tools/check_dram.py
scripts/check atlas       # core feeds atlas/data/facts.json
```

The device build needs ESP-IDF 5.5 in the shell: `. "$IDF_PATH/export.sh"` (for example
`~/esp/esp-idf`). A SKIP means it did not run, not that it passed.

## Where new code goes

- Watch screen: `components/core/src/ui_<name>.c`, its logic in a pure `<name>.c` with
  `simulator/tests/test_<name>.c` and a `simulator/scripts/<name>_*.txt` (S-NEW-01).
- HAL operation: `components/agentlink_hal/include/hal.h`, then every board, the simulator and
  `atlas/wasm/web_hal.c` in one commit (S-NEW-02, S-DEP-10).
- Board: `boards/<name>/`, built with `-DAGENTLINK_BOARD=<name>` (S-NEW-05).
- Protocol message: follow S-NEW-03; never edit `agentlink_proto` output by hand.

## Rules that break most often here

- S-DEP-01 / C-MOD-07: `core` includes only `hal.h`, LVGL and `agentlink_proto`.
- S-CORE-03: `ui_*.c` never includes glue headers (`app.h`, `link.h`, `power.h` …).
- C-SAFE-11: only `core_step()`'s thread touches LVGL; HAL callbacks push into a ring.
- C-SAFE-01: no heap after init; big static tables go in PSRAM (`MODEL_EXT_BSS`).

## Gotchas

- **Glyphs over words.** Every screen you add or change starts from glyphs, colours and shapes;
  icons come from `ui_icons.h` (root [AGENTS.md](../AGENTS.md), UI-V3 §1.7).
- **After every flash, read the serial boot log for asserts.** The simulator cannot catch
  internal-RAM (DRAM) limits: a build that leaves too little boot-loops in `BLE_INIT`.
- `firmware/tools/check_dram.py <build>/agentlink.map` keeps the floor of free internal RAM BLE
  needs at boot (100 KB). If it fails, move tables to PSRAM; do not lower the floor.
- **The simulator cannot show timing bugs.** A `power_tick()` unsigned underflow turned the display
  off on the watch only (UX-PROFILE §1); `power.c` now compares clocks with the wrap-safe `passed()`.
  Compare `uint32_t` millisecond clocks the same way, and time things with the bench.
- A hand-run daemon holding the USB serial port blocks `idf.py flash`. Stop it (by its PID) first.
- After any firmware edit, run `python3 atlas/tools/extract.py` and commit `atlas/data/facts.json`.
- `boards/stopwatch/sw_power` must not see M5GFX headers, and the board needs `WHOLE_ARCHIVE`
  (FIRMWARE.md §12).
