// RX8130CE RTC (I2C 0x32). The chip always holds UTC.
#include <stdint.h>

#include "esp_log.h"

#include "board_priv.h"
#include "hal.h"

static const char *TAG = "sw-rtc";

#define RX8130_ADDR 0x32
#define REG_SEC 0x10
#define REG_FLAG 0x1D
#define REG_CTRL0 0x1E
#define BIT_FLAG_VLF (1 << 1)
#define BIT_CTRL_STOP (1 << 6)

/* 2024-01-01: anything earlier means the RTC was never set. */
#define RTC_MIN_VALID_UNIX 1704067200u

static i2c_master_dev_handle_t s_dev;

static bool rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100) == ESP_OK;
}

static bool wr(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t tmp[8];
    if (len > 7) {
        return false;
    }
    tmp[0] = reg;
    for (size_t i = 0; i < len; i++) {
        tmp[1 + i] = data[i];
    }
    return i2c_master_transmit(s_dev, tmp, len + 1, 100) == ESP_OK;
}

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0f)); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Howard Hinnant's civil-date algorithms (UTC, no libc TZ involvement). */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

bool sw_rtc_init(void)
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = RX8130_ADDR;
    cfg.scl_speed_hz = SW_I2C_DEV_HZ;
    if (i2c_master_bus_add_device(sw_i2c_bus(), &cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "add device failed");
        s_dev = nullptr;
        return false;
    }
    uint8_t flag = 0;
    if (!rd(REG_FLAG, &flag, 1)) {
        ESP_LOGE(TAG, "RX8130 not responding");
        return false;
    }
    return true;
}

uint32_t hal_rtc_get_unix(void)
{
    uint8_t flag = 0, r[7];
    if (!s_dev || !rd(REG_FLAG, &flag, 1) || (flag & BIT_FLAG_VLF) || !rd(REG_SEC, r, 7)) {
        return 0;
    }
    int year = 2000 + bcd2dec(r[6]);
    unsigned month = bcd2dec(r[5] & 0x1f);
    unsigned mday = bcd2dec(r[4] & 0x3f);
    if (month < 1 || month > 12 || mday < 1 || mday > 31) {
        return 0;
    }
    int64_t t = days_from_civil(year, month, mday) * 86400 + bcd2dec(r[2] & 0x3f) * 3600 + bcd2dec(r[1] & 0x7f) * 60 +
                bcd2dec(r[0] & 0x7f);
    return (t < RTC_MIN_VALID_UNIX || t > UINT32_MAX) ? 0 : (uint32_t)t;
}

void hal_rtc_set_unix(uint32_t unix_s)
{
    if (!s_dev) {
        return;
    }
    int64_t days = unix_s / 86400;
    uint32_t sod = unix_s % 86400;
    int year;
    unsigned month, day;
    civil_from_days(days, &year, &month, &day);
    unsigned wday = (unsigned)((days + 4) % 7); /* 1970-01-01 was a Thursday */

    uint8_t regs[7] = {dec2bcd(sod % 60),
                       dec2bcd((sod / 60) % 60),
                       dec2bcd(sod / 3600),
                       (uint8_t)(1u << wday), /* weekday is one-hot on the RX8130 */
                       dec2bcd(day),
                       dec2bcd(month),
                       dec2bcd((uint8_t)(year % 100))};

    uint8_t ctrl = 0;
    rd(REG_CTRL0, &ctrl, 1);
    uint8_t stop = ctrl | BIT_CTRL_STOP;
    wr(REG_CTRL0, &stop, 1);
    wr(REG_SEC, regs, 7);
    uint8_t run = ctrl & (uint8_t)~BIT_CTRL_STOP;
    wr(REG_CTRL0, &run, 1);
    uint8_t flag = 0;
    if (rd(REG_FLAG, &flag, 1) && (flag & BIT_FLAG_VLF)) {
        flag &= (uint8_t)~BIT_FLAG_VLF;
        wr(REG_FLAG, &flag, 1);
    }
}
