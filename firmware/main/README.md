# firmware/main

The ESP-IDF entry point. `app_main()` initialises the board, LVGL, the display and touch, then runs
the `core` app loop. Design: [FIRMWARE.md](../../docs/design/FIRMWARE.md).

## Build and test

```sh
. "$IDF_PATH/export.sh"
scripts/check firmware    # = idf.py -C firmware build && python3 firmware/tools/check_dram.py
```

After a flash, read the serial boot log for asserts ([firmware/AGENTS.md](../AGENTS.md)).

| File | Concern |
|---|---|
| `main.c` | `app_main()`: board init, LVGL init, the core loop; an LVGL "hello" screen when built without core |
| `CMakeLists.txt` | REQUIRES `agentlink_hal`, the selected board (`AGENTLINK_BOARD`), LVGL and `core` unless `AGENTLINK_NO_CORE` is set |
| `idf_component.yml` | Managed dependencies: ESP-IDF 5.5 or newer, LVGL 9.5 |

## Rules that bind it

- S-DEP-06: includes `hal.h`, `lvgl.h`, ESP-IDF and the public `core.h` only, never `core/src/*`.

No generated files.
