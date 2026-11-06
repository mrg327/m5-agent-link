> Source: https://docs.m5stack.com/en/uiflow2/uiflow_web

# UiFlow2 Web IDE Introduction

## 1. UiFlow2 Introduction

UiFlow2 is a web-based integrated development environment (IDE) for graphical programming. Built on Blockly's drag-and-drop block programming, it supports deploying programs to devices over wired or wireless connections. Without manual compilation or coding, users can quickly control M5Stack devices and a wide range of sensor modules. Compared with UiFlow 1, UiFlow2 adds project sharing and device sharing to facilitate team collaboration and improve development efficiency.

**UiFlow2 Web IDE Interface Preview:**

## 2. Workflow

Before programming with UiFlow2, flash the firmware and complete the required configuration as follows:

1. Download and install M5Burner.
2. Open the getting started guide for your device.
3. Follow the device guide to flash the UiFlow2 firmware and configure the network or obtain the Access Code.
4. Connect the device to UiFlow2 Web IDE using the Access Code or USB, then start programming and debugging.

## 3. Download M5Burner

M5Burner is M5Stack's official firmware flashing tool. It can flash UiFlow2 firmware to devices and preset device configuration such as Wi-Fi during flashing.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1145/m5burner_intro_pic_01.jpg)

Download the M5Burner version for your operating system and follow the installer instructions. For Windows and Linux, extract the downloaded package before running the application.

| Software Version | Download Link |
| --- | --- |
| M5Burner\_Windows | [Download](https://m5burner-cdn.m5stack.com/app/M5Burner-v3-beta-win-x64.zip) |
| M5Burner\_MacOS | [Download](https://m5burner-cdn.m5stack.com/app/M5Burner-v3-mac-x64.dmg) |
| M5Burner\_Linux | [Download](https://m5burner-cdn.m5stack.com/app/M5Burner-v3-beta-linux-x64.zip) |

## 4. Device Quick Start

The steps for entering download mode, flashing UiFlow2 firmware, and obtaining an Access Code vary by device. Select your device and open its getting started guide to complete firmware flashing, device connection, and program execution.

### Core Controllers

#### Core

[![](https://static-cdn.m5stack.com/resource/docs/products/core/basic/basic_cover_01.webp)Basic](https://docs.m5stack.com/en/uiflow2/m5core/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/core2/core2_cover_01.webp)Core2](https://docs.m5stack.com/en/uiflow2/m5core2/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/tough/tough_cover_01.webp)Tough](https://docs.m5stack.com/en/uiflow2/m5tough/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1275/K162_ToughC5_main_pictures_02.webp)ToughC5](https://docs.m5stack.com/en/uiflow2/toughc5/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/CoreS3/img-2f6b5728-af80-4934-854f-4771a7fe859e.webp)CoreS3](https://docs.m5stack.com/en/uiflow2/m5cores3/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/fire/fire_cover_01.webp)Fire](https://docs.m5stack.com/en/uiflow2/m5fire/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/M5CoreMP135/img-574f31f6-f05d-4f07-b648-f5ea4467514d.webp)CoreMP135](https://docs.m5stack.com/en/compute/coremp135/uiflow2) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1274/C158_CoreMatrix_main_pictures_02.webp)CoreMatrix](https://docs.m5stack.com/en/uiflow2/corematrix/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/M5NanoC6/img-cb3ecdbe-cea7-47d6-b07c-5fb860420c61.webp)NanoC6](https://docs.m5stack.com/en/uiflow2/nanoc6/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/C145_04.webp)Tab5](https://docs.m5stack.com/en/uiflow2/Tab5/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1183/C124-K124-main-picture_19.webp)PowerHub](https://docs.m5stack.com/en/uiflow2/powerhub/program)

#### Atom

[![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1182/Atom_EchoS3R_02.webp)Atom VoiceS3R](https://docs.m5stack.com/en/uiflow2/atom_echos3r/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/ATOM Lite/img-85e2d7f3-6562-4b51-bf1c-5f7988605dd0.webp)Atom-Lite](https://docs.m5stack.com/en/uiflow2/atomlite/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/ATOM Matrix/img-c3488647-a432-47c1-8990-a873a8e62804.webp)Atom-Matrix](https://docs.m5stack.com/en/uiflow2/atommatrix/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/ATOM U/img-9ee4c97b-6c3b-4342-80e2-01a9b9ba5a9a.webp)AtomU](https://docs.m5stack.com/en/uiflow2/atomu/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/AtomS3/3.webp)AtomS3](https://docs.m5stack.com/en/uiflow2/atoms3/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/AtomS3R/3.webp)AtomS3R](https://docs.m5stack.com/en/uiflow2/atoms3r/program) [![](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/core/AtomS3R%20Cam/12.webp)AtomS3R-CAM/AtomS3R-M12](https://docs.m5stack.com/en/uiflow2/atoms3r-cam/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/AtomS3U/img-b6c6a17f-c469-4825-b6e9-5b8eadd5b266.webp)AtomS3U](https://docs.m5stack.com/en/uiflow2/atoms3u/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/AtomS3 Lite/img-ae1b2014-3c47-4f9f-8506-c6ec0667ac09.webp)AtomS3-Lite](https://docs.m5stack.com/en/uiflow2/atoms3lite/program)

#### Stick

[![](https://static-cdn.m5stack.com/resource/docs/products/core/m5stickc/m5stickc_cover_01.webp)StickC](https://docs.m5stack.com/en/uiflow2/m5stickc/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/m5stickc_plus/m5stickc_plus_cover_01.webp)StickC-Plus](https://docs.m5stack.com/en/uiflow2/m5stickcplus/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/M5StickC%20PLUS2/3.webp)StickC-Plus2](https://docs.m5stack.com/en/uiflow2/m5stickcplus2/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150-stickS3_main-products_02.webp)StickS3](https://docs.m5stack.com/en/uiflow2/sticks3/program)

#### Stamp

[![](https://static-cdn.m5stack.com/resource/docs/products/core/StampS3/img-e720ad06-5e51-491f-a6d8-697102dfde2b.webp)Stamp-S3](https://docs.m5stack.com/en/uiflow2/stamps3/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1218/S013_main-pictures_02.webp)Stamp-P4](https://docs.m5stack.com/en/uiflow2/stamp_p4/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1129/K141_04.webp)StamPLC](https://docs.m5stack.com/en/uiflow2/stamplc/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/527/C050-B_13.jpg)Stamp-Pico](https://docs.m5stack.com/en/uiflow2/stamppico/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/M5Dial/img-de17f26a-87d2-4d15-bdf0-1da1307fd969.webp)Dial](https://docs.m5stack.com/en/uiflow2/m5dial/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/M5Capsule/img-2071b849-e525-481a-a197-2eed1b133027.webp)Capsule](https://docs.m5stack.com/en/uiflow2/m5capsule/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/AirQ/img-5e0c9a17-efe3-46af-8322-c6f7c143fc2c.webp)Air Quality](https://docs.m5stack.com/en/uiflow2/airq/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/M5DinMeter/img-dff307df-3121-4405-b12f-eee5e136a321.webp)DinMeter](https://docs.m5stack.com/en/uiflow2/m5dinmeter/program)

#### Cardputer

[![](https://static-cdn.m5stack.com/resource/docs/products/core/Cardputer/img-a02d6727-4fa1-4b38-90e6-75e401d507de.webp)Cardputer](https://docs.m5stack.com/en/uiflow2/cardputer/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Cardputer-Adv_02.webp)Cardputer-Adv](https://docs.m5stack.com/en/uiflow2/cardputer-adv/program)

#### E-Paper

[![](https://static-cdn.m5stack.com/resource/docs/products/core/m5paper/m5paper_cover_01.webp)Paper](https://docs.m5stack.com/en/uiflow2/m5paper/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1239/C151_main-pictures_02.webp)PaperColor](https://docs.m5stack.com/en/uiflow2/papercolor/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1288/K159-B_PaperDIY_Complete_Kit_main_pictures_02.webp)PaperDIY](https://docs.m5stack.com/en/uiflow2/paperdiy/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/PaperS3/3.webp)PaperS3](https://docs.m5stack.com/en/uiflow2/m5papers3/program) [![](https://static-cdn.m5stack.com/resource/docs/products/core/coreink/coreink_cover_01.webp)CoreInk](https://docs.m5stack.com/en/uiflow2/m5coreink/program)

#### Other

[![](https://static-cdn.m5stack.com/resource/docs/products/core/station_bat/station_bat_cover_01.webp)Station-Bat](https://docs.m5stack.com/en/uiflow2/m5station/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152_stopwatch_mainpictures_02.webp)StopWatch](https://docs.m5stack.com/en/uiflow2/stopwatch/program) [![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DK001_main_pictures_01.jpg)Arduino Nesso N1](https://docs.m5stack.com/en/uiflow2/arduino_nesso_n1/program)

### Unit Series

#### Wireless Communication

[![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1185/U202-Unit-C6L_02.webp)Unit C6L](https://docs.m5stack.com/en/uiflow2/unit_c6l/program)

#### Ethernet

[![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1217/U213_unit_poe-p4_main_pictures_02.webp)Unit PoE-P4](https://docs.m5stack.com/en/uiflow2/unit_poe_p4/program)

### Hobby Kits

#### Robotics

[![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/K151_stack_chan_main_pictures_03.webp)StackChan](https://docs.m5stack.com/en/uiflow2/stackchan/program)

### Chain Series

#### Human-Machine Interaction

[![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1176/C147_chain-dualkey-mainpicture_02.webp)Chain DualKey](https://docs.m5stack.com/en/uiflow2/chain_dualKey/program)

## 5. Related Links

* [UiFlow2 Web IDE](https://uiflow2.m5stack.com/)
* [UiFlow2 Development Guide](https://docs.m5stack.com/en/uiflow2/uiflow2_layout)
* [M5Burner Firmware Export](https://docs.m5stack.com/en/uiflow/m5burner/export)
* [M5Burner Firmware Publishing](https://docs.m5stack.com/en/uiflow/m5burner/publish)
