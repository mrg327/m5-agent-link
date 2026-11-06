# agent-link firmware (ESP-IDF)

Target: M5Stack StopWatch (C152, ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, 466x466 CO5300 AMOLED).
ESP-IDF release/v5.5 (tested with 5.5.6), LVGL 9.5.

## Build and flash

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32s3 build flash monitor     # add -p /dev/cu.usbmodem101 if needed
```

- Device port: `/dev/cu.usbmodem101` (USB Serial/JTAG console).
- Download mode: hold the power button until the green LED blinks.

## Options

- `-DAGENTLINK_BOARD=<name>` selects `boards/<name>` (default `stopwatch`).
- `-DAGENTLINK_NO_CORE=ON` skips `components/core` and `agentlink_proto` and builds an LVGL
  "hello" screen. Core is used automatically when `components/core/CMakeLists.txt` exists.
  Use `idf.py fullclean` (or delete `build/`) when switching.

## Layout

- `main/` entry point: board init, LVGL init, display, touch, then the core app loop.
- `boards/stopwatch/` BSP implementing `components/agentlink_hal/include/hal.h` (M5GFX display, CST820 touch,
  M5PM1 PMIC, M5IOE1 expander, RX8130 RTC, buttons, haptic).
- `components/agentlink_hal/` header-only contract. Its directory name equals ESP-IDF's own `hal`
  component, so the top-level `CMakeLists.txt` excludes it from discovery and grafts its include
  dir onto IDF's `hal` target. Renaming it (e.g. `agentlink_hal`) would remove this workaround.

## Managed dependencies

lvgl/lvgl 9.5.0, m5stack/m5gfx 0.2.19, m5stack/M5PM1 (git tag 1.0.6), m5stack/M5IOE1 (git tag 1.0.8),
espressif/i2c_bus 1.5.2 (transitive; not used at runtime).

## Stubs (TODO)

IMU motion/lift detection, sound, BLE and USB link (`hal_link_*` return false), DEEP power state.
