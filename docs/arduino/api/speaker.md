> Source: https://docs.m5stack.com/en/arduino/stopwatch/speaker

# StopWatch Speaker

StopWatch Speaker related APIs and sample programs.

## Example

### Compilation Requirements

* M5Stack Board Manager Version >= 3.3.7
* Board Option = M5StopWatch
* M5Unified Library Version >= 0.2.15
* M5GFX Library Version >= 0.2.21

```cpp
#include "M5Unified.h"

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextFont(&fonts::FreeMonoBold18pt7b);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Speaker Test", M5.Display.width() / 2,
                                M5.Display.height() / 2);
}

void loop() {
    M5.Speaker.tone(10000, 100);
    delay(1000);
    M5.Speaker.tone(4000, 20);
    delay(1000);
}
```

After successful upload, you will hear sounds at different frequencies from the speaker, specifically 10kHz and 4kHz.

## API

The StopWatch Speaker section uses the `Speaker_Class` from the M5Unified library. For more related APIs, refer to the following document:

* [M5Unified - Speaker Class](https://docs.m5stack.com/en/arduino/m5unified/speaker_class)
