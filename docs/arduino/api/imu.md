> Source: https://docs.m5stack.com/en/arduino/stopwatch/imu

# StopWatch IMU

StopWatch IMU-related input APIs and sample programs.

## Example

### Compilation Requirements

* M5Stack Board Manager Version >= 3.3.7
* Board Option = M5StopWatch
* M5Unified Library Version >= 0.2.15
* M5GFX Library Version >= 0.2.21

```cpp
#include <M5Unified.h>

void setup(void) {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
    Serial.begin(115200);
}

void loop(void) {
    auto imu_update = M5.Imu.update();
    if (imu_update) {
        auto data = M5.Imu.getImuData();

        // The data obtained by getImuData can be used as follows.
        data.accel.x;      // accel x-axis value.
        data.accel.y;      // accel y-axis value.
        data.accel.z;      // accel z-axis value.
        data.accel.value;  // accel 3values array [0]=x / [1]=y / [2]=z.

        data.gyro.x;      // gyro x-axis value.
        data.gyro.y;      // gyro y-axis value.
        data.gyro.z;      // gyro z-axis value.
        data.gyro.value;  // gyro 3values array [0]=x / [1]=y / [2]=z.

        data.value;  // all sensor 9values array [0~2]=accel / [3~5]=gyro / [6~8]=mag

        Serial.printf("ax: %f  ay: %f  az: %f\r\n", data.accel.x, data.accel.y, data.accel.z);
        Serial.printf("gx: %f  gy: %f  gz: %f\r\n", data.gyro.x, data.gyro.y, data.gyro.z);
        M5.Display.setCursor(40, 120);
        M5.Display.clear();  // Delay 100ms
        M5.Display.printf("IMU:\r\n");
        M5.Display.printf("       ax: %f\n       ay: %f\n       az: %f\r\n", data.accel.x, data.accel.y, data.accel.z);
        M5.Display.printf("       gx: %f\n       gy: %f\n       gz: %f\r\n", data.gyro.x, data.gyro.y, data.gyro.z);
    }
    delay(300);
}
```

After uploading successfully, you can view the IMU sensor acceleration and gyroscope data in the serial monitor and on the screen.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/StopWatch_Arduino_imu.jpg)

## API

The StopWatch IMU section uses the `IMU_Class` from the `M5Unified` library. For more IMU-related APIs, refer to the following document:

* [M5Unified - IMU Class](https://docs.m5stack.com/en/arduino/m5unified/imu_class)
