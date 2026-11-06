> Source: https://docs.m5stack.com/en/guide/stopwatch/restore_factory

# StopWatch Restore Factory Firmware

Factory Firmware

When the device is not working properly, you can try re-flashing the factory firmware to check whether the device hardware is faulty. Refer to the following tutorial. Use the M5Burner firmware flashing tool to flash the factory firmware to the device.

## 1. Preparation

* Refer to the [M5Burner Tutorial](https://docs.m5stack.com/en/uiflow/m5burner/intro) to download the flashing tool, and download the corresponding firmware as shown below.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_burner_factory_01.jpg)

## 2. Download Mode

Connect the device to the computer via a USB Type-C cable, then press and hold the reset button (approximately 2 seconds) until the green LED lights up, then release. The device has now entered download mode and is ready for flashing.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C132-download-mode.gif)

## 3. Firmware Flashing

1. Click the Burn button for the corresponding firmware in M5Burner.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_burner_factory_02.jpg)

2. Select the corresponding device port, then click `Start` to begin flashing.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_burner_factory_03.jpg)

After flashing is complete, the interface will display `Burn successfully, click here to return`.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_burner_factory_04.jpg)

3. Press the reset button on the device. After the device restarts, the result is shown below:

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/stopwatch_burner_factory_05.jpg)

## Related Reference

For detailed usage instructions of the StopWatch factory firmware, refer to the [Tutorial](https://docs.m5stack.com/en/guide/stopwatch/usage).
