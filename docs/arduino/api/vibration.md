> Source: https://docs.m5stack.com/en/arduino/stopwatch/vibration

# StopWatch Vibration

StopWatch vibration motor related APIs and sample programs.

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

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Vibration Test", M5.Display.width() / 2, M5.Display.height() / 2);
}

void loop(void) {
  M5.Power.setVibration(10);
  delay(500);
  M5.Power.setVibration(40);
  delay(500);
  M5.Power.setVibration(70);
  delay(500);
  M5.Power.setVibration(100);
  delay(500);
  M5.Power.setVibration(130);
  delay(500);
  M5.Power.setVibration(160);
  delay(500);
  M5.Power.setVibration(190);
  delay(500);
  M5.Power.setVibration(210);
  delay(500);
  M5.Power.setVibration(240);
  delay(500);
}
```

The StopWatch vibration motor loops with gradually increasing intensity, as shown below:

[](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/StopWatch_Arduino_vibration.mp4)

## API

The StopWatch vibration motor section uses the `Power_Class` from the `M5Unified` library. For more related APIs, refer to the following document:

* [M5Unified - Power Class](https://docs.m5stack.com/en/arduino/m5unified/power_class)
