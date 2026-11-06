> Source: https://docs.m5stack.com/en/arduino/stopwatch/battery

# StopWatch Battery Status

StopWatch battery status related APIs and sample programs.

## Example

### Compilation Requirements

* M5Stack Board Manager Version >= 3.3.7
* Board Option = M5StopWatch
* M5Unified Library Version >= 0.2.15
* M5GFX Library Version >= 0.2.21
* M5PM1 Library Version >= 1.0.7

```cpp
#include <M5Unified.h>
#include <M5PM1.h>

M5PM1 pm1;

bool highCurrent = false;   // false: 185mA (default), true: 425mA

// PYG3 -> LGS4056H PROG: Hi-Z = 185mA, LOW = 425mA
void setChargeCurrent(bool high)
{
    if (high) {
        pm1.gpioSetFunc(M5PM1_GPIO_NUM_3, M5PM1_GPIO_FUNC_GPIO);
        pm1.gpioSetMode(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT);
        pm1.gpioSetPull(M5PM1_GPIO_NUM_3, M5PM1_GPIO_PULL_NONE);
        pm1.gpioSetDrive(M5PM1_GPIO_NUM_3, M5PM1_GPIO_DRIVE_PUSHPULL);
        pm1.gpioSetOutput(M5PM1_GPIO_NUM_3, 0);
        // Equivalent: pm1.gpioSet(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
    } else {
        pm1.gpioSetFunc(M5PM1_GPIO_NUM_3, M5PM1_GPIO_FUNC_GPIO);
        pm1.gpioSetMode(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_INPUT);
        pm1.gpioSetPull(M5PM1_GPIO_NUM_3, M5PM1_GPIO_PULL_NONE);
    }
}

void drawLabels()
{
    M5.Lcd.setRotation(0);
    M5.Lcd.setFont(&fonts::FreeMonoBold12pt7b);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.drawString("StopWatch Realtime", M5.Lcd.width() / 2, 50);
    M5.Lcd.drawString("Battery Status\n",  M5.Lcd.width() / 2, 80);
    M5.Lcd.setCursor(40, 170);
    M5.Lcd.print("Battery Charging:");
    M5.Lcd.setCursor(40, 210);
    M5.Lcd.print("Battery    Level:");
    M5.Lcd.setCursor(40, 250);
    M5.Lcd.print("Battery  Voltage:");
    M5.Lcd.setCursor(40, 290);
    M5.Lcd.print("Charge  Current:");
    M5.Lcd.drawString("BtnA: switch 185 / 425 mA", M5.Lcd.width() / 2, 330);
}

void drawRuntime()
{
    M5.Lcd.setCursor(290, 170);
    M5.Lcd.printf("%s \n", M5.Power.isCharging() ? "Yes" : "No");

    M5.Lcd.setCursor(290, 210);
    M5.Lcd.printf("%d %%\n", M5.Power.getBatteryLevel());

    M5.Lcd.setCursor(290, 250);
    M5.Lcd.printf("%d mV\n", M5.Power.getBatteryVoltage());

    M5.Lcd.setCursor(290, 290);
    M5.Lcd.printf("%d mA\n", highCurrent ? 425 : 185);
}

void setup()
{
    M5.begin();
    Serial.begin(115200);

    // M5PM1 is initialized by M5.begin(); use its GPIO API for PYG3
    pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    pm1.setChargeEnable(true);
    setChargeCurrent(highCurrent);   // Default to 185mA
    pm1.setSingleResetDisable(false);  // Enable single reset: short press power button resets M5PM1

    drawLabels();
    drawRuntime();
}

void loop()
{
    M5.update();

    if (M5.BtnA.wasPressed()) {
        highCurrent = !highCurrent;
        setChargeCurrent(highCurrent);

        M5.Lcd.setCursor(290, 290);
        M5.Lcd.printf("%d mA\n", highCurrent ? 425 : 185);

        Serial.printf("Charge current setting: %d mA\n", highCurrent ? 425 : 185);
    }

    // Refresh every 2 seconds
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();
        drawRuntime();
    }
}
```

This program displays whether the battery is charging, the battery level percentage, the battery voltage, and the current charge-current setting on the screen, refreshing every 2 seconds. Press button A (BtnA) to switch the charge current between the 185mA and 425mA settings.

Note

The 425mA setting is maintained by the M5PM1 PYG3 pin. After a short press of the power button (M5PM1 reset), a double-click power-off, entering download mode, or a power loss, PYG3 returns to high impedance, and the charge current reverts to the default 185mA.

![](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/StopWatch_Arduino_battery.jpg)

## API

The StopWatch battery status section uses the `Power_Class` from the `M5Unified` library. For more related APIs, refer to the following document:

* [M5Unified - Power Class](https://docs.m5stack.com/en/arduino/m5unified/power_class)
