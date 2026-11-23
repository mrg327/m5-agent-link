# firmware/boards

Board support packages: each `<name>/` is an ESP-IDF component that implements
[`hal.h`](../components/agentlink_hal/include/hal.h) on real hardware. The build picks one with
`-DAGENTLINK_BOARD=<name>` (default `stopwatch`). Design: [FIRMWARE.md](../../docs/design/FIRMWARE.md)
(§12 lists the StopWatch gotchas).

## Build and test

```sh
. "$IDF_PATH/export.sh"
scripts/check firmware    # = idf.py -C firmware build && python3 firmware/tools/check_dram.py
```

A board can only be tested on the device: flash it, read the boot log, then run the bench
([BENCH.md](../../docs/design/BENCH.md)).

| Path | Concern |
|---|---|
| `stopwatch/` | M5Stack StopWatch (ESP32-S3): `board.cpp`, `display.cpp` (CO5300 by M5GFX), `touch.cpp`, `input.cpp`, `imu.cpp` (BMI270), `audio.cpp`, `mic.cpp`, `rtc.cpp` (RX8130), `ota.cpp`, `link.cpp` with `link_usb.cpp` and `link_ble.cpp`, `Kconfig` (display options) |
| `stopwatch/sw_power/` | M5PM1 power chip and M5IOE1 expander; a separate component so it never sees M5GFX headers |

## Rules that bind it

- S-DEP-05: a board includes `hal.h`, its own headers, its sub-components and ESP-IDF; never `core`
  or `agentlink_proto`.
- S-DEP-10 / S-NEW-02: it defines every `hal.h` function; a missing feature is a `caps` flag and a
  no-op.
- S-NEW-05: a new board is `boards/<name>/`, a component named `<name>` that REQUIRES
  `agentlink_hal`, with an optional `firmware/sdkconfig.<name>`.
- The StopWatch component is registered `WHOLE_ARCHIVE`, or the linker drops the `hal_*` functions.
