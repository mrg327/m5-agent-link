# M5Stack StopWatch (SKU C152) documentation

This is a local mirror of the vendor documentation for the M5Stack StopWatch. To regenerate it, run `uv run scripts/fetch_docs.py`.

## Quick facts

- **SoC:** ESP32-S3R8 (dual-core LX7 @ 240 MHz), 16 MB flash, 8 MB octal PSRAM, 2.4 GHz Wi-Fi
- **Peripherals:** 1.75" 466×466 AMOLED (CO5300, QSPI), CST820B touch, BMI270 IMU, RX8130CE RTC, ES8311 codec + MEMS mic + AW8737A amp/speaker, vibration motor, 2 user buttons + power button
- **Power:** M5PM1 power-management IC; M5IOE1 I/O expander (drives display/touch reset and amp enable); 450 mAh battery
- **USB:** native ESP32-S3 USB-Serial/JTAG (VID `0x303a`, PID `0x1001`), seen on macOS as `/dev/cu.usbmodem*`
- **Download mode:** hold the power button until the green LED blinks, then release it
- **Power on/off:** single-click the power button to power on or reset; double-click it to power off
- **Arduino:** board `M5StopWatch` (M5Stack board manager ≥ 3.3.7); libraries M5Unified ≥ 0.2.15, M5GFX ≥ 0.2.21, M5PM1, M5IOE1
- **PlatformIO:** see the `[env:m5stack-stopwatch]` block in [StopWatch.md](StopWatch.md#platformio)

## Contents

| Path | What |
| --- | --- |
| [StopWatch.md](StopWatch.md) | Product page: specs, **pin map**, M5PM1/M5IOE1 pins, PlatformIO config |
| [StopWatch_product_page.pdf](StopWatch_product_page.pdf) | PDF export of the product page |
| [hardware/](hardware/) | Schematic and model/size drawing |
| [datasheets/](datasheets/) | ESP32-S3, M5PM1, M5IOE1, ES8311, RX8130CE, CO5300, CST820B |
| [guide/](guide/) | Factory firmware usage, factory firmware restore, Xiaozhi voice assistant |
| [arduino/setup/](arduino/setup/) | Arduino IDE, board manager, and library manager setup |
| [arduino/program.md](arduino/program.md) | Compile and upload walkthrough |
| [arduino/api/](arduino/api/) | Per-peripheral APIs and examples: battery, button, display, IMU, mic, RTC, speaker, touch, vibration, M5PM1/M5IOE1 |
| [uiflow2/](uiflow2/) | UiFlow2 (MicroPython) quick start |
| [tools/m5burner.md](tools/m5burner.md) | M5Burner firmware flashing tool |
| [esp_idf/](esp_idf/) | ESP-IDF index (factory firmware and low-power examples are on GitHub) |
| [github/](github/) | READMEs: M5Unified, M5GFX, M5PM1, M5IOE1, M5StopWatch-UserDemo (ESP-IDF factory firmware), M5StopWatch-PowerDemo, Hermes voice agent |

Sources: <https://docs.m5stack.com/en/core/StopWatch> and the linked GitHub repositories.
