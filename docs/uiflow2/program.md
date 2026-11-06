> Source: https://docs.m5stack.com/en/uiflow2/stopwatch/program

# StopWatch: Flash Firmware & Run Code

## 1. UiFlow2 Firmware Burning

1. Refer to the [UiFlow2 Web IDE Tutorial](https://docs.m5stack.com/en/uiflow2/uiflow_web) to learn the basic workflow of using UiFlow2 and complete the installation of the M5Burner firmware burning tool.
2. Connect the device to the computer using a USB Type-C data cable. Press and hold the reset button for about 2 seconds until the green LED lights up, then release it. The device has now entered download mode and is waiting for firmware burning.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C132-download-mode.gif)

3. Download the firmware compatible with `StopWatch` in M5Burner, as shown below.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_01.jpg)

4. Click the `Burn` button corresponding to the `StopWatch` firmware, select the corresponding device port, and click `Start`.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_02.jpg)

5. Fill in the Wi-Fi configuration required by the device, including Wi-Fi SSID and Wi-Fi Password, as well as any other device configurations that need to be added or modified. Then click `Next` to start burning.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_04.jpg)

Configuration information description:

* COM: Select the serial port corresponding to the device
* BaudRate: Baud rate for serial communication
* Server: Server address that the device connects to
* WIFI SSID/WIFI Password: Wi-Fi name and password that the device connects to
* SNTP Server:
  + SNTP0: Alibaba Cloud NTP server (China)
  + SNTP1: Japan NTP server pool
  + SNTP2: Global public NTP server pool
* Timezone: Time zone setting
* Boot Option: Set the device startup mode after firmware burning is complete
  + Run main.py directly: After burning is complete, directly run the program in main.py without connecting to the network or displaying the UiFlow2 startup screen
  + Show startup menu and network setup: The device connects to the network, and devices with a screen will display the UiFlow2 startup screen
  + Only network setup: The device only connects to the network and does not display the UiFlow2 startup screen

6. When the prompt `Burn successfully, click here to return` appears, the burning is successful. Restart the device at this point to apply the burned configuration.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_05.jpg)

7. After firmware burning is complete, if you need to modify the device configuration, keep the USB connection, restart the device, click the `Configure` option, and modify the settings according to the on-screen prompts.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_06.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_07.jpg)

## 2. Device Connection

The device supports connecting to UiFlow2 via `Access Code` (over a wireless network) or `USB` (wired connection to the computer) for program deployment and debugging. Refer to the following tutorial steps for details:

### Access Code Wireless Connection

1. Before connecting, check the Wi-Fi icon on the screen to confirm that the device is connected to the network. If the network connection is not successful, check whether the Wi-Fi name and password entered during firmware burning are correct, modify them through the `Configure` option in M5Burner, and burn the firmware again.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_44.jpg)![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_43.jpg)

2. Visit [uiflow2.m5stack.com](https://uiflow2.m5stack.com/) to open the UiFlow2 Web IDE.
3. Click `Select Your Controller` (displayed when entering for the first time) or the `Controller` button on the page to enter the `Select Device` page.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1145/m5burner_intro_pic_34.jpg)

4. Click `Connect Device`, enter the access code and a custom device name, then click `Confirm` to connect StopWatch to UiFlow2.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1145/m5burner_intro_pic_36.jpg) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_45.jpg)

Access Code Description

1. Changing browsers, using private / incognito mode, or clearing the UiFlow website cache will invalidate the original pairing state.  
2. Only closing the tab, restarting the browser, logging in, or logging out of the account will not affect the existing bound pairing relationship.  
3. Once the device is bound and paired by another browser, the pairing relationship in the original browser will immediately become invalid.

5. Select the connected `StopWatch` device on the `Select Device` page, then click `Confirm` to enter the UiFlow2 programming interface.

### USB Wired Connection

1. Visit [uiflow2.m5stack.com](https://uiflow2.m5stack.com/) to open the UiFlow2 Web IDE, and connect **StopWatch** to the computer via a USB Type-C cable.
2. Select the **StopWatch** device from the device list, then click `Confirm`.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_08.jpg)

3. Click the `WebTerminal` button, select the serial port of `StopWatch` in the dialog box, and click `Connect`.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_11.jpg)

When the WebTerminal screen displays `Connected to Serial Port!`, the USB connection is successful.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_12.jpg)

## 3. Program Running and Downloading

After the device is connected to UiFlow2, you can drag Blockly blocks to edit the program.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_17.gif) ![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_20.jpg)

After program editing is complete, click the `Run Once` button in the lower-right corner of the interface to run and test the program once. Click the `Run Always` button to download the program to the device.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_42.jpg)

You can also run once or download the program from the **WebTerminal** window.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_uiflow2_docs_41.jpg)

## 4. Related Links

* [UiFlow2 Blockly Introduction](https://uiflow-micropython.readthedocs.io/en/latest/)
* [UiFlow2 Firmware Source Code](https://github.com/m5stack/uiflow-micropython/tree/master)

## 5. Related Videos

Bilibili
Youtube
