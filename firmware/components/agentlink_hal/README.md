# agentlink_hal

The header-only contract between portable firmware code (`components/core`) and whatever runs it: a
board under `firmware/boards/`, the desktop simulator, or the browser build in `atlas/wasm/`.
Design: [FIRMWARE.md](../../../docs/design/FIRMWARE.md).

## Build and test

It has no sources. Every HAL is checked by linking: `scripts/check sim` (simulator),
`scripts/check firmware` (boards) and `atlas/wasm/build.sh` (browser) each fail on a missing function.

| File | Concern |
|---|---|
| `include/hal.h` | Board identity and `caps`, time, display and touch (LVGL), buttons, motion, haptics and sound, stored preferences, microphone, power, link transports, OTA, debug lines |
| `CMakeLists.txt` | ESP-IDF interface component; REQUIRES `lvgl__lvgl` only |

## Rules that bind it

- S-DEP-03: includes only the C library and `lvgl.h`, nothing else from the project.
- S-DEP-10: every function here is defined by every HAL (each board, `simulator/src/sim_*.c`,
  `atlas/wasm/web_hal.c`). A feature a board lacks is a `caps` flag plus a no-op.
- S-NEW-02: a new operation gets a Doxygen comment saying which context may call it and what a board
  without the feature does, and lands in every HAL in the same commit.
- C-SAFE-11: callbacks may run in an ISR or a driver task; they copy into a ring and never call LVGL.

No generated files.
