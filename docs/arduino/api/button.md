> Source: https://docs.m5stack.com/en/arduino/stopwatch/button

# StopWatch Button

StopWatch button input related APIs and sample programs.

Note

Include the `M5.update()` function in the main loop to read state updates and minimize blocking as much as possible. Otherwise, button state changes may not be captured in time.

## Example

### Compilation Requirements

* M5Stack Board Manager Version >= 3.3.7
* Board Option = M5StopWatch
* M5Unified Library Version >= 0.2.15
* M5GFX Library Version >= 0.2.21

```cpp
#include "M5Unified.h"

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
}

void loop()
{
    M5.update();
    if (M5.BtnA.wasPressed()) {
        Serial.println("BtnA Pressed");
    }
    if (M5.BtnB.wasPressed()) {
        Serial.println("BtnB Pressed");
    }
}
```

To see the effect, upload the example, press the buttons, and view the output in the serial monitor.

## API

The StopWatch button section uses the `Button_Class` from the M5Unified library. For more button-related APIs, refer to the following document:

* [M5Unified - Button Class](https://docs.m5stack.com/en/arduino/m5unified/button_class)
