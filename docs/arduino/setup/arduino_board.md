> Source: https://docs.m5stack.com/en/arduino/arduino_board

# Arduino Board Management

Arduino IDE Board Manager:

**The Arduino IDE Board Manager** is used to record the configuration information of development boards and the toolchains used during the development process. Refer to the tutorial below to install the M5Stack board manager. The online installation method is simpler and allows for one-click installation. In some regions, if network issues prevent normal downloading, you might try switching to a mobile network or configure a proxy in the Preferences menu.

## 1. Installing Board Management

**1. The Board Manager URL is used to index the development board information for a specific platform. In the Arduino IDE menu, select `File` -> `Preferences`**

![](https://static-cdn.m5stack.com/resource/docs/static/assets/img/arduino/arduino_ide_02.webp)

**2. Copy the M5Stack board management URL below into the `Additional Board Manager URLs` field**, and save.

```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

China Regional Mirror Resources

Users in China may experience connection and download speed issues when using the above links. Please use the following mirror resource URLs instead.

```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index_cn.json
```

![](https://static-cdn.m5stack.com/resource/docs/static/assets/img/arduino/arduino_ide_03.webp) ![](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/assets/img/arduino/arduino_ide_board_url.jpg)

**3. In the sidebar, select `Board Manager`, search for `M5Stack`, and click `Install`.**

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Arduino_board.png)

## 2. Selecting the Development Board

**Depending on the product used, select the corresponding development board under `Tools` -> `Board` -> `M5Stack` -> `{Product Name}`**.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Arduino_selectboard.png)

Board Option

The board management list may not include the latest development board options. If there is no corresponding product board option, `you can select a board with the same chip type for program compilation`, such as `M5Dial` uses the `ESP32S3` chip, so you can use the M5StampS3 board option for compilation.

## 3. Manual Installation of Board Manager

Manual Installation of Board Manager

If the M5Stack Board Manager cannot be downloaded properly due to network issues, you can manually install it using the following method.

**1. Download the Board Manager Archive**

* [Google Drive - M5Stack Board Manager](https://drive.google.com/drive/folders/1p-mJ30os1rCQcKgcM2imAXZkGNUZWEzY)
* [Baidu Netdisk - M5Stack Board Manager, Extraction Code: f7rd](https://pan.baidu.com/s/1KspFcwiw-C2YSdvZeiKmDA?pwd=f7rd)

**2. Extract the Board Manager Archive**, extract the downloaded M5Stack Board Manager package and place it in the following directory.

* Windows OS (Enter the following path in the File Explorer address bar and press Enter to directly access the directory):

```
%USERNAME%\AppData\Local\Arduino15\packages
```

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Arduino_board_manual.png)

* Mac OS (Make sure to replace `<Username>` in the path with your actual username):

```
/Users/<Username>/Library/Arduino15/packages
```

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1145/arduino_tutorial_01.png)

Special Reminder:

The extracted folder name must be `m5stack`; otherwise, the related library files will not work properly.

**3. Restart Arduino IDE** and you will be able to see the M5Stack control board.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Arduino_selectboard_manual.png)
