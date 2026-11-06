> Source: https://docs.m5stack.com/en/core/StopWatch

# StopWatch

SKU:C152

## Description

StopWatch is a round AMOLED touch development board designed for portable and interactive scenarios. It features an ESP32-S3R8 main controller with 16MB Flash and 8MB PSRAM, supporting 2.4GHz Wi-Fi wireless communication. The device integrates a 1.75" AMOLED touch round screen, programmable buttons, and vibration feedback for an intuitive human-machine interaction experience. It also features audio input/output, IMU attitude sensing, RTC timekeeping, and multiple expansion interfaces. Combined with the M5PM1 multi-level power management system and a 450mAh battery, it meets diverse development needs including portable smart control, wearable human-machine interaction, and lightweight IoT terminals.

## Note

Declaration on Sticker Printing

StopWatch exists in **two hardware versions, v1.0 and v1.0.1**:  
**v1.0**: The body sticker on some batches of the product contains a printing error: the position indicated by the arrow in the image below is labeled BAT, but this pin actually functions as **5V IN**, an external 5V power input interface rather than a battery pin. Never connect a battery to it.  
**v1.0.1**: The hardware has been revised, and this pin is now correctly defined as `BAT`, which is the battery pin.

| Version | Sticker Mark | Actual Function |
| --- | --- | --- |
| `v1.0` | `BAT` | `5V IN` |
| `v1.0` | `5V IN` | `5V IN` |
| `v1.0.1` | `*BAT` | `BAT` |

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152_StopWatch_Note_01.png)

## Tutorial

![UiFlow2](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/assets/img/uiflow2/uiflow2.0_banner_01.png) [UiFlow2](https://docs.m5stack.com/en/uiflow2/stopwatch/program)

[UiFlow2](https://docs.m5stack.com/en/uiflow2/stopwatch/program)

This tutorial introduces how to control the StopWatch device through the UiFlow2 graphical programming platform.

![Arduino IDE](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/assets/img/arduino/arduino_banner_01.png) [Arduino IDE](https://docs.m5stack.com/en/arduino/stopwatch/program)

[Arduino IDE](https://docs.m5stack.com/en/arduino/stopwatch/program)

This tutorial explains how to program and control the StopWatch device using Arduino IDE.

![Xiaozhi Voice Assistant](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch-Xiaozhi_Cover_01.jpg) [Xiaozhi Voice Assistant](https://docs.m5stack.com/en/guide/stopwatch/xiaozhi_voice_assistant)

[Xiaozhi Voice Assistant](https://docs.m5stack.com/en/guide/stopwatch/xiaozhi_voice_assistant)

This tutorial covers flashing the Xiaozhi Voice Assistant firmware onto StopWatch via M5Burner to build a personal voice assistant application.

![Factory Firmware Usage Tutorial](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_06.jpg) [Factory Firmware Usage Tutorial](https://docs.m5stack.com/en/guide/stopwatch/usage)

[Factory Firmware Usage Tutorial](https://docs.m5stack.com/en/guide/stopwatch/usage)

This tutorial covers the factory firmware usage for StopWatch, including button operations and feature descriptions.

![Hermes Voice Agent Tutorial](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152_Hermes_doc_cover.png) [Hermes Voice Agent](https://github.com/allinnb/hermes-gadget-sdk/blob/main/STOPWATCH.md)

[Hermes Voice Agent](https://github.com/allinnb/hermes-gadget-sdk/blob/main/STOPWATCH.md)

This tutorial explains how to run the Hermes voice agent firmware on StopWatch for voice conversation, including the required steps and precautions.

## Features

* ESP32-S3R8 Core Controller:
  + 16MB Flash + 8MB PSRAM
  + 2.4GHz Wi-Fi
* Interactive Experience:
  + 1.75" AMOLED Touch Round Screen
  + 2 x Programmable Buttons, 1 x Power Button
  + Built-in Vibration Motor
  + 6-axis IMU BMI270
* Audio System:
  + ES8311 Audio Codec
  + MEMS MIC
  + AW8737A Speaker Amplifier + 1W @ 8Ω Speaker
* RTC Clock Chip RX8130CE
* Expansion Capability:
  + HY2.0-4P Expansion Interface
  + Rear Expansion Bus (2.54-7P, 2.54-6P)
  + Lanyard Hole Design
  + Rear Magnetic Attachment Design
* Power System:
  + M5PM1 Multi-level Power Management System
  + Built-in 450mAh Battery
  + Balanced Battery Life and Low Power Operation

## Includes

* 1 x StopWatch

## Applications

* Portable Smart Devices
* Electronic Badges
* Lightweight IoT Terminals

## Specification

| Specification | Parameter |
| --- | --- |
| SoC | ESP32-S3R8 @ Xtensa® 32-bit LX7 dual-core processor, clock frequency 240MHz |
| Flash | 16MB |
| PSRAM | 8MB |
| Wireless Connectivity | 2.4GHz Wi-Fi |
| Display | 1.75" AMOLED Touch Round Screen 466x466 Resolution, CO5300 Driver Chip QSPI Interface |
| Touch | CST820B Driver Chip |
| Buttons | 2 x Programmable Buttons + 1 x Power Button |
| Vibration Feedback | Built-in Vibration Motor |
| Audio Codec | ES8311 |
| Microphone | MEMS MIC |
| Speaker | AW8737A Amplifier, 8Ω@1W 1318 Cavity Speaker |
| IMU | BMI270 |
| RTC | RX8130CE |
| Expansion Interface | HY2.0-4P |
| Rear Expansion Bus | 2.54-7P, 2.54-6P |
| Body Structure | Lanyard Hole |
| Power Management | M5PM1 Multi-level Power Management |
| Battery Capacity | 450mAh |
| Input Power | USB Type-C DC 5V |
| Product Size | 52.0 x 52.0 x 15.5mm |
| Product Weight | 39.0g |
| Package Size | 70.0 x 70.0 x 23.6mm |
| Gross Weight | 49.5g |

## Learn

### Power On/Off

* Single click the power button: Power on / Reset
* Double-click the power button: Power off

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C132-poweroff-on.gif)

### Enter Download Mode

* Connect the device to a computer via a USB Type-C data cable. Press and hold the power button until the green LED blinks, then release it. The device enters download mode.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C132-download-mode.gif)

## Schematics

* [StopWatch Schematics PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-SCH_Stopwatch_PRJ_Main_VA_20251201_2026_04_24_17_46_22.pdf)

## PinMap

### AMOLED

| ESP32S3R8 | G39 | G40 | G38 | G41 | G42 | G46 | G45 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| CO5300 | OLED\_CS | OLED\_SCK | OLED\_TE | OLED\_D0 | OLED\_D1 | OLED\_D2 | OLED\_D3 |

The display reset is controlled via `PYG5 (PYB_OLED_RST)` of the `M5IOE1` expansion chip.

### Touch

| ESP32S3R8 | G47 | G48 | G13 |
| --- | --- | --- | --- |
| CST820B | SYS\_SDA | SYS\_SCL | TP\_INT |

The touch reset is controlled via `PYG4 (PYB_TP_RST)` of the `M5IOE1` expansion chip.

### Audio

| ESP32S3R8 | G48 | G47 |
| --- | --- | --- |
| ES8311(0x18) | AUDIO\_I2C\_SCL | AUDIO\_I2C\_SDA |

| ESP32S3R8 | G18 | G17 | G16 | G15 | G21 |
| --- | --- | --- | --- | --- | --- |
| ES8311 | I2S\_MCLK | I2S\_BCLK | I2S\_ASDOUT | I2S\_LRCK | I2S\_DSDIN |

The speaker amplifier (AW8737A) enable is controlled via `PYG10(PYB_SPK_EN)` of the `M5IOE1` expansion chip, and the Audio L3B level power supply for audio peripherals is controlled via `PYG3(PYB_AU_EN)`.

### KEY

| ESP32S3R8 | G2 | G1 |
| --- | --- | --- |
| KEY | KEYA (YELLOW) | KEYB (BLUE) |

### IMU & RTC

| ESP32S3R8 | G48 | G47 |
| --- | --- | --- |
| BMI270(0x68) | SYS\_SCL | SYS\_SDA |
| RX8130CE(0x32) | SYS\_SCL | SYS\_SDA |

### HY2.0-4P

| HY2.0-4P | Black | Red | Yellow | White |
| --- | --- | --- | --- | --- |
| PORT.A | GND | 5V | G10 | G11 |

### EXT 2.54 Bus

| FUNC | PIN | LEFT | RIGHT | PIN | FUNC |
| --- | --- | --- | --- | --- | --- |
|  | 3V3\_L2 | 1 | 2 | G3 |  |
| G43(UART0\_TX) / G20(USB\_DP) | MUX\_IO\_1 | 3 | 4 | G4 |  |
| G44(UART0\_RX) / G19(USB\_DM) | MUX\_IO\_2 | 5 | 6 | G5 |  |
|  | G9 | 7 | 8 | G6 |  |
| BOOT | G0 | 9 | 10 | G7 |  |
|  | GND | 11 | 12 | G8 |  |
|  | / | 13 | 14 | BAT |  |

The `MUX_IO_1, MUX_IO_2` pin switching between `USB / UART` functions is controlled via `PYG1(PYB_MUX_CTR)` of the `M5IOE1` expansion chip. The default function is `UART`.

| PYG1\_HAT\_SW | EXT MUX\_IO\_1/2 FUNC |
| --- | --- |
| LOW | G43\_U0TXD / G44\_U0RXD |
| HIGH | USB\_P / USB\_N |

### M5PM1

| ESP32S3R8 | G48 | G47 |
| --- | --- | --- |
| M5PM1 | SYS\_SCL | SYS\_SDA |

| M5PM1 | DCDC3V3\_EN\_PP | LDO3V3\_EN\_PP | BOOST5V\_EN\_PP |
| --- | --- | --- | --- |
| 3V3\_L2 | PM\_3V3\_L2\_EN |  |  |
| 3V3\_L1 |  | PM\_3V3\_L1\_EN |  |
| Grove |  |  | PM\_EXT\_5V\_EN |

| M5PM1 | PMG0 | PMG1 | PMG2 | PMG3 | PMG4 |
| --- | --- | --- | --- | --- | --- |
| RTC & IMU | RTC\_INT & IMU\_INT |  |  |  |  |
| ESP32S3R8 |  | G12\_PY\_IRQ |  |  |  |
| CHARGE |  |  | CHG\_STAT | CHG\_PROG |  |
| EXT 5VIN |  |  |  |  | PORT\_INT |

* PMG0(RTC\_INT & IMU\_INT): Interrupt wake-up signal
* PMG1(G12\_PY\_IRQ): Interrupt signal connected to ESP32-S3
* PMG2(CHG\_STAT): Charging status
* PMG3\_CHG\_PROG: Charging current control
* PMG4(PORT\_INT): External power wake-up signal

| PMG3\_CHG\_PROG | CHARGE CURRENT |
| --- | --- |
| LOW | 425mA |
| High Impedance | 185mA (Default) |

StopWatch charge current setting notes

To set the `425mA` charge current: set `M5PM1_GPIO_NUM_3` to `OUTPUT` + `PUSHPULL` mode and drive it low.

```
pm1.gpioSetFunc(M5PM1_GPIO_NUM_3, M5PM1_GPIO_FUNC_GPIO);
pm1.gpioSetMode(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT);
pm1.gpioSetPull(M5PM1_GPIO_NUM_3, M5PM1_GPIO_PULL_NONE);
pm1.gpioSetDrive(M5PM1_GPIO_NUM_3, M5PM1_GPIO_DRIVE_PUSHPULL);
pm1.gpioSetOutput(M5PM1_GPIO_NUM_3, 0);
// pm1.gpioSet(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT, 1, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
```

To set the `185mA` charge current: set `M5PM1_GPIO_NUM_3` to `INPUT` + `PULL_NONE` mode.

```
pm1.gpioSetFunc(M5PM1_GPIO_NUM_3, M5PM1_GPIO_FUNC_GPIO);
pm1.gpioSetMode(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_INPUT);
pm1.gpioSetPull(M5PM1_GPIO_NUM_3, M5PM1_GPIO_PULL_NONE);
```

### M5IOE1

| ESP32S3R8 | G48 | G47 |
| --- | --- | --- |
| M5IOE1(0x4F) | SYS\_SCL | SYS\_SDA |

| M5IOE1 | PYG1 | PYG3 | PYG9 | PYG8 | PYG10 | PYG4 | PYG5 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ext.Port Select | PYB\_MUX\_CTR |  |  |  |  |  |  |
| Audio L3B |  | PYB\_AU\_EN |  |  |  |  |  |
| Vibration Motor |  |  | PYB\_MT\_PWM |  |  |  |  |
| 3V3\_L3B |  |  |  | PYB\_L3B\_EN |  |  |  |
| Speaker AMP AW8737A |  |  |  |  | PYB\_SPK\_EN |  |  |
| Touch |  |  |  |  |  | PYB\_TP\_RST |  |
| AMOLED |  |  |  |  |  |  | PYB\_OLED\_RST |

* PYG1(PYB\_MUX\_CTR): Rear expansion interface MUX\_IO\_1/2 switching between `USB / UART` functions
* PYG3(PYB\_AU\_EN): ES8311 power supply + MIC power supply
* PYG9(PYB\_MT\_PWM): Vibration motor PWM signal
* PYG8(PYB\_L3B\_EN): AMOLED screen power supply
* PYG10(PYB\_SPK\_EN): AW8737A speaker amplifier enable
* PYG4(PYB\_TP\_RST): Touch screen reset
* PYG5(PYB\_OLED\_RST): Display screen reset

## Model Size

* [StopWatch Model Size PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-StopWatch-model-size.pdf)

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-StopWatch-model-size_page_01.png)

## Structure

* [StopWatch Structure Files](https://github.com/m5stack/M5_Hardware/tree/master/Products/C152_StopWatch/Structures)

## Datasheets

* [ESP32-S3](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/472/esp32-s3_datasheet_en.pdf)
* [ES8311](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/atom/Atomic%20Echo%20Base/ES8311.pdf)
* [RX8130CE Datasheet](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/RX8130CE_cn.pdf)
* [RX8130CE Register Manual](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/RX8130CE_cn-Register-Datasheet.pdf)
* [CO5300 Display Driver Datasheet](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/CO5300_Datasheet.pdf)
* [CST820 Touch Controller Datasheet](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/CST820B_datasheet.pdf)

## Softwares

### Quick Start

* [StopWatch Xiaozhi Voice Assistant](https://docs.m5stack.com/en/guide/stopwatch/xiaozhi_voice_assistant)

### Arduino

* [StopWatch Arduino Quick Start](https://docs.m5stack.com/en/arduino/stopwatch/program)
* [StopWatch M5PM1 & M5IOE1 Power Management](https://docs.m5stack.com/en/arduino/stopwatch/m5pm1_m5ioe1)
* [M5PM1 Arduino Library](https://github.com/m5stack/M5PM1)
* [M5IOE1 Arduino Library](https://github.com/m5stack/M5IOE1)
* [StopWatch Arduino M5Unified Driver Library](https://github.com/m5stack/M5Unified)
* [StopWatch Arduino M5GFX Driver Library](https://github.com/m5stack/M5GFX)

### UiFlow2

* [StopWatch UiFlow2 Quick Start](https://docs.m5stack.com/en/uiflow2/stopwatch/program)

### Protocol

* [M5PM1 Power Management Chip](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf)
* [M5IOE1 IO Expansion Management Chip](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1210/IO_Expander_Datasheet_EN.pdf)

### ESP-IDF

* [StopWatch Factory Firmware](https://github.com/m5stack/M5StopWatch-UserDemo)
* [StopWatch Low-Power Test Example](https://github.com/m5stack/M5StopWatch-PowerDemo)

### PlatformIO

```
[env:m5stack-stopwatch]
platform = espressif32 @ 6.12.0
board = esp32s3box
framework = arduino
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.arduino.memory_type = qio_opi
monitor_speed = 115200
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Unified = https://github.com/m5stack/M5Unified
    M5GFX = https://github.com/m5stack/M5GFX
    M5PM1 = https://github.com/m5stack/M5PM1
    M5IOE1 = https://github.com/m5stack/M5IOE1
```

### Easyloader

| Easyloader | Download | Note |
| --- | --- | --- |
| StopWatch User Demo | [download](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-StopWatch-UserDemo.exe) | / |

### Other

* [StopWatch Factory Firmware Usage Tutorial](https://docs.m5stack.com/en/guide/stopwatch/usage)
* [StopWatch Factory Firmware Flashing Tutorial](https://docs.m5stack.com/en/guide/stopwatch/restore_factory)

## Video

## Version Change

| Release Date | Product Change |
| --- | --- |
| 2026-08-27 | v1.0.1 hardware version released, 5V IN pin definition changed to BAT |
| 2026-08-10 | v1.0 hardware version sticker correction, 5V BAT on the sticker changed to 5V IN |
| 2026-05-22 | v1.0 hardware version released |
