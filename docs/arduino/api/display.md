> Source: https://docs.m5stack.com/en/arduino/stopwatch/display

# StopWatch Display

Description

StopWatch uses the M5GFX library as the display driver. Refer to the API and example below to implement simple display functions. For more API details, refer to the [M5GFX](https://docs.m5stack.com/en/arduino/m5gfx/m5gfx_functions) documentation.

## Example

### Compilation Requirements

* M5Stack Board Manager Version >= 3.3.7
* Board Option = M5StopWatch
* M5Unified Library Version >= 0.2.15
* M5GFX Library Version >= 0.2.21

```cpp
#include <M5Unified.h>

void draw_function(LovyanGFX* gfx) {
    int x      = rand() % gfx->width();
    int y      = rand() % gfx->height();
    int r      = (gfx->width() >> 4) + 2;
    uint16_t c = rand();
    gfx->fillRect(x - r, y - r, r * 2, r * 2, c);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    int textsize = M5.Display.height() / 60;
    if (textsize == 0) {
        textsize = 1;
    }
    M5.Display.setTextSize(textsize);
}

void loop() {
    int x      = rand() % M5.Display.width();
    int y      = rand() % M5.Display.height();
    int r      = (M5.Display.width() >> 4) + 2;
    uint16_t c = rand();
    M5.Display.fillCircle(x, y, r, c);
    draw_function(&M5.Display);
}
```

The example effect is shown below. The program draws circles and rectangles of different colors at random positions on the screen:

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/StopWatch_Arduino_display.jpg)
