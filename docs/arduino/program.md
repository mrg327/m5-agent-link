> Source: https://docs.m5stack.com/en/arduino/stopwatch/program

# StopWatch Arduino Example Program Compilation and Upload

## 1. Preparation

* 1. Arduino IDE Installation: Refer to the [Arduino IDE Installation Tutorial](https://docs.m5stack.com/en/arduino/arduino_ide) to complete the IDE installation.
* 2. Board Manager Installation: Refer to the [Basic Environment Setup Tutorial](https://docs.m5stack.com/en/arduino/arduino_board) to complete the M5Stack Board Manager installation and select the `M5StopWatch` board.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/quickstart_arduino_StopWatch_select_board.png)

* 3. Dependency Library Installation: Refer to the [Library Manager Installation Tutorial](https://docs.m5stack.com/en/arduino/arduino_library) to complete the installation of the `M5Unified` and `M5GFX` libraries, and install all dependent libraries as prompted.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/StopWatch_arduino_lib_01.png) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/StopWatch_arduino_lib_02.png)

## 2. Download Mode

* Press and hold the reset button for about 2 seconds until the internal green LED lights up, then release it. The device will enter download mode and wait for uploading.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C132-download-mode.gif)

## 3. Port Selection

Connect the device to the computer with a USB cable, then select the corresponding device port in Arduino IDE.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/quickstart_arduino_StopWatch_select_port.png)

## 4. Program Compile & Upload

Open the example program ?BarGraph? in the library and click the upload button. The IDE will automatically compile and upload the program.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/quickstart_arduino_StopWatch_select_demo.png) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/quickstart_arduino_StopWatch_select_demoupload.png)

The example effect is shown below:

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/quickstart_arduino_StopWatch_example.jpg)

## 5. Related Resources

* Arduino Library
  + [M5Unified](https://github.com/m5stack/M5Unified)
* Arduino API & Examples
  + [Battery](https://docs.m5stack.com/en/arduino/stopwatch/battery)
  + [Button](https://docs.m5stack.com/en/arduino/stopwatch/button)
  + [Display](https://docs.m5stack.com/en/arduino/stopwatch/display)
  + [IMU](https://docs.m5stack.com/en/arduino/stopwatch/imu)
  + [MIC](https://docs.m5stack.com/en/arduino/stopwatch/mic)
  + [RTC](https://docs.m5stack.com/en/arduino/stopwatch/rtc)
  + [Speaker](https://docs.m5stack.com/en/arduino/stopwatch/speaker)
  + [Touch](https://docs.m5stack.com/en/arduino/stopwatch/touch)
  + [Vibration](https://docs.m5stack.com/en/arduino/stopwatch/vibration)
  + [M5PM1 & M5IOE1](https://docs.m5stack.com/en/arduino/stopwatch/m5pm1_m5ioe1)
