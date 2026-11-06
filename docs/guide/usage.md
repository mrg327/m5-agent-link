> Source: https://docs.m5stack.com/en/guide/stopwatch/usage

# StopWatch Factory Firmware Tutorial

## Introduction

StopWatch is an AMOLED round touch development board designed for portable and interactive scenarios. The factory firmware integrates features such as a digital badge, stopwatch, and alarm clock, making it suitable for portable smart control, wearable HMI interaction, lightweight IoT terminals, and other development applications. This tutorial introduces how to use the factory firmware on the StopWatch device.

## Button Operations

* After powering on, press and hold Button A and Button B at the same time to enter the main menu.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_27.jpg)

* Press Button A / Button B, or tap the left and right on-screen buttons, to switch between function menus by sliding left or right.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_01_s0_fast1_5.gif)

* Tap a function icon on the screen to enter the function detail page. Press and hold Button A and Button B at the same time to return to the main menu. Swipe from the top edge of the device toward the center to **display the battery level**.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_02.gif)

## Using the Badge Function

The StopWatch badge function allows you to connect to the device AP, upload local images, and display them.

1. Tap the `Badge` menu to enter the badge function.
2. Press and hold the screen. A dialog will appear asking whether to enter the badge editing interface. Tap `Edit`. The device will automatically start the AP hotspot `M5StopWatch-xxxx`. Connect to the hotspot with a phone or computer, then visit `192.168.4.1` in a browser to open the badge management interface.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_28.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/152-factory-doc_12.jpg)

3. Click the `Slot` where you want to upload an image, then click `Choose File` and select the image to upload. Use the `Zoom` function to adjust the displayed image size, and use the `Background` function to adjust the image background color.
4. After configuration is complete, click `Upload To Selected Slot` to upload the image. Repeat the steps above to upload images to multiple slots.
5. After the image upload is complete, click `Done` at the bottom of the interface. The image upload is successful.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/152-factory-doc_13.jpg)

Image upload complete:

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_14.jpg)

Effect demonstration:

[](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_15.mp4)

## Function Menu Introduction

* **AlarmClock**: Alarm clock function. Click the Add button to add multiple alarms.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_16.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_03.jpg)

* **WatchFace**: Clock function. Press Button A/Button B to switch the time display style. Tap the screen to switch the clock background color/style.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_25.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_05.jpg)

* **Stopwatch**: Digital stopwatch.
  + **START**: Start timing
  + **RESET**: Reset data
  + **LAP**: Lap button
  + **STOP**: Stop timing

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_22.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_04.jpg)

* **Badge**: Digital badge. Images can be uploaded and displayed. When uploading from a mobile device, the image size must be less than 3MB.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_26.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_31.jpg)

* **IMU**: IMU attitude display interface. It displays real-time three-axis acceleration data and visually reflects the current tilt and spatial attitude of the device.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_19.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_11.jpg)

* **Audio.FFT**: Measures audio tone frequency. Tap the center area of the interface to show or hide the frequency value.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_20.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_08.jpg)

* **LuckyWheel**: Lucky wheel. After customizing the number of wheel sections, tap the center wheel icon to start spinning.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_23.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_09.jpg)

* **Settings**: Device-related settings, including screen brightness, device volume, time/date, firmware version query, and more.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_24.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-factory-doc_10.jpg)
