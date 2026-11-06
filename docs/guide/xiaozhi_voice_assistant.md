> Source: https://docs.m5stack.com/en/guide/stopwatch/xiaozhi_voice_assistant

# StopWatch Xiaozhi Voice Assistant

This tutorial will guide you through flashing the Xiaozhi Voice Assistant firmware onto the StopWatch development board using M5Burner to build a personal voice assistant application.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch-Xiaozhi_Cover_01.jpg)

## 1. Preparation

Please download the appropriate M5Burner firmware flashing tool for your operating system by clicking the button below, then extract and open the application.

| Software Version | Download Link |
| --- | --- |
| M5Burner\_Windows | [Download](https://m5burner-cdn.m5stack.com/app/M5Burner-v3-beta-win-x64.zip) |
| M5Burner\_MacOS | [Download](https://m5burner-cdn.m5stack.com/app/M5Burner-v3-mac-x64.dmg) |
| M5Burner\_Linux | [Download](https://m5burner-cdn.m5stack.com/app/M5Burner-v3-beta-linux-x64.zip) |

## 2. Firmware Flashing

1. Double-click to open the Burner tool, select the device type `STOPWATCH` from the left menu, and click `Download` to download the StopWatch Xiaozhi Voice Assistant.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_xiaozhi_tutorial_01.jpg)

2. Connect the device to a computer via USB Type-C cable, press and hold the reset button (approximately 2 seconds) until the green LED lights up, then release. The device has now entered download mode and is ready for flashing.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C132-download-mode.gif)

3. Click `Burn`, select the corresponding port for the device, then click `Start` and wait for the flashing process to complete.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_xiaozhi_tutorial_04.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_xiaozhi_tutorial_05.jpg)

## 3. Wi-Fi Configuration

1. After the device starts up, it will prompt you to connect to an AP hotspot. Connect to the hotspot **Xiaozhi-xxx** from your phone/computer, or visit `192.168.4.1` to access the network configuration page.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-xiaozhi_tutorial_06.jpg)

2. Follow the on-screen instructions to complete the Wi-Fi configuration.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-xiaozhi_assistant_wifi_config_02.jpg)

## 4. Register Xiaozhi AI

1. Visit the [Xiaozhi AI Control Panel](https://xiaozhi.me/), register and log in to your account.
2. Obtain the device verification code. After successful Wi-Fi configuration, the device will automatically announce "Please log in to the control panel to add the device, enter verification code xxx". Devices with a screen will also display the verification code on screen.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-xiaozhi_tutorial_07.jpg)

3. In the Xiaozhi AI Control Panel, click `Console` > `Add Device` in sequence, and enter the verification code displayed on the device to complete device binding.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1214/echo_pyramid_xiaozhi_tutorial_04.png) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1214/echo_pyramid_xiaozhi_tutorial_05.png) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1214/echo_pyramid_xiaozhi_tutorial_09.png)

## 5. Getting Started

After completing the above configuration, wake up the assistant with the wake word "Hi Xiaozhi" to start a conversation.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-xiaozhi_tutorial_08.jpg)

## 6. Voice Switching

Xiaozhi AI provides several voice templates. You can configure them on the `Configure Role` page in the control panel. Note: The device must be restarted for changes to take effect.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1214/echo_pyramid_xiaozhi_tutorial_10.png) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1214/echo_pyramid_xiaozhi_tutorial_11.png) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1214/echo_pyramid_xiaozhi_tutorial_12.png)
