/* Power report (docs/design/POWER.md): state-time accounting, the report cadence (at the welcome, every 5 min,
 * 30 s after a refused send, none to a daemon below 0.19, figures kept while unlinked) and the figures a deep
 * sleep keeps in RTC memory. Builds power_report.c against stubbed HAL, link, power and UI; no simulator. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "link.h"
#include "power_report.h"

/* ── stubs: the state the report reads ── */
static bool s_display_on = true, s_dimmed, s_welcomed, s_send_ok = true;
static uint8_t s_minor = 19;
static uint32_t s_itvl_us, s_slept_ms, s_battery_reads;
static hal_wake_t s_wake = HAL_WAKE_POWER_ON;
static uint32_t s_words[HAL_RETAINED_WORDS];
static link_power_t s_sent[16];
static int s_n_sent, s_n_tries;

bool power_display_on(void) { return s_display_on; }
bool ui_dimmed(void) { return s_dimmed; }
bool link_welcomed(void) { return s_welcomed; }
uint8_t model_peer_minor(void) { return s_minor; }
uint32_t hal_link_ble_interval_us(void) { return s_itvl_us; }
hal_wake_t hal_wake_reason(void) { return s_wake; }
uint32_t hal_deep_slept_ms(void) { return s_slept_ms; }
uint32_t *hal_retained_words(void) { return s_words; }
hal_battery_t hal_battery_read(void)
{
    s_battery_reads++;
    return (hal_battery_t){.percent = 63, .charging = false, .usb_present = false};
}
hal_battery_mv_t hal_battery_mv(void) { return (hal_battery_mv_t){.mv = 3871, .mv_filt = 3880, .vin_mv = 0}; }
bool link_send_power(const link_power_t *p)
{
    s_n_tries++;
    if (!s_welcomed || s_minor < LINK_POWER_MINOR || !s_send_ok) return false;
    if (s_n_sent < 16) s_sent[s_n_sent] = *p;
    s_n_sent++;
    return true;
}
void dbg_event(const char *c, const char *e, const char *f, ...) { (void)c; (void)e; (void)f; }

static int failures;
#define EXPECT(cond, msg)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);                   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static uint32_t s_now;
/* Runs the core loop's ticks for ms, one every step_ms, as core_step does (50 ms with the display off). */
static void run(uint32_t ms, uint32_t step_ms)
{
    for (uint32_t end = s_now + ms; s_now < end;) {
        s_now += step_ms;
        preport_tick(s_now);
    }
}

static void boot(void)
{
    s_n_sent = s_n_tries = 0;
    s_now = 1000;
    preport_init(s_now);
}

static void test_account_pure(void)
{
    preport_times_t t = {0};
    preport_account(&t, PREPORT_ON, true, false, 100);
    preport_account(&t, PREPORT_DIM, true, true, 200);
    preport_account(&t, PREPORT_OFF, false, true, 300);
    EXPECT(t.on == 100 && t.dim == 200 && t.off == 300, "each state gets its own time");
    EXPECT(t.lk == 300 && t.ble == 500, "linked and BLE time overlap the states");
    t.off = UINT32_MAX - 5;
    preport_account(&t, PREPORT_OFF, false, false, 100);
    EXPECT(t.off == UINT32_MAX, "saturates instead of wrapping");
}

static void test_pack_unpack(void)
{
    uint32_t w[HAL_RETAINED_WORDS] = {0};
    preport_times_t t = {.on = 1, .dim = 2, .off = 3, .deep = 4, .lk = 5, .ble = 6, .nd = 7}, u;
    uint32_t seq = 0;
    EXPECT(!preport_unpack(w, &u, &seq), "zeroed RTC words (a power-on) hold no figures");
    preport_pack(&t, 42, w);
    EXPECT(preport_unpack(w, &u, &seq), "packed figures unpack");
    EXPECT(!memcmp(&t, &u, sizeof t) && seq == 42, "round trip");
}

static void test_cadence(void)
{
    s_welcomed = false;
    s_display_on = true;
    s_dimmed = false;
    s_itvl_us = 0;
    boot();
    run(20000, 20);
    EXPECT(s_n_tries == 0, "nothing goes out before a welcome");
    s_welcomed = true;
    preport_on_link(true, s_now);
    run(20, 20);
    EXPECT(s_n_sent == 1, "a report right after the welcome");
    EXPECT(s_sent[0].seq == 0 && s_sent[0].on >= 20000 && s_sent[0].lk <= 40, "the first covers the time since boot");
    EXPECT(s_sent[0].mv == 3871 && s_sent[0].mvf == 3880 && s_sent[0].pct == 63, "battery voltages and percent");
    run(PREPORT_EVERY_MS - 40, 20);
    EXPECT(s_n_sent == 1, "not before 5 minutes");
    run(40, 20);
    EXPECT(s_n_sent == 2, "every 5 minutes while linked");
    EXPECT(s_sent[1].seq == 1, "seq counts the reports");
    uint32_t sum = s_sent[1].on + s_sent[1].dim + s_sent[1].off + s_sent[1].deep;
    EXPECT(sum >= PREPORT_EVERY_MS - 20 && sum <= PREPORT_EVERY_MS + 20, "a report covers the 5 minutes since the last");
    EXPECT(s_sent[1].lk == sum, "linked the whole time");

    /* a refused send: the figures stay and the next try is 30 s later */
    s_send_ok = false;
    run(PREPORT_EVERY_MS, 20);
    EXPECT(s_n_sent == 2 && s_n_tries == 3, "the send was tried and refused");
    run(PREPORT_RETRY_MS - 40, 20);
    EXPECT(s_n_tries == 3, "no retry before 30 s");
    s_send_ok = true;
    run(40, 20);
    EXPECT(s_n_sent == 3, "retried after 30 s");
    sum = s_sent[2].on + s_sent[2].dim + s_sent[2].off;
    EXPECT(sum >= PREPORT_EVERY_MS + PREPORT_RETRY_MS - 40, "nothing was lost to the refusal");
}

static void test_states_and_unlinked(void)
{
    s_welcomed = true;
    s_minor = 19;
    s_itvl_us = 30000;
    boot();
    preport_on_link(true, s_now);
    run(20, 20); /* report 0 */
    s_display_on = true;
    s_dimmed = false;
    run(30000, 20);
    s_dimmed = true;
    run(60000, 20);
    s_display_on = false;
    s_dimmed = false;
    s_itvl_us = 0; /* the central went away: unlinked from here */
    s_welcomed = false;
    preport_on_link(false, s_now);
    run(600000, 50);
    EXPECT(s_n_sent == 1, "nothing while unlinked");
    s_welcomed = true;
    s_itvl_us = 45000;
    preport_on_link(true, s_now);
    run(50, 50);
    EXPECT(s_n_sent == 2, "a new link hears at once");
    const link_power_t *p = &s_sent[1];
    EXPECT(p->on >= 29980 && p->on <= 30020, "30 s on");
    EXPECT(p->dim >= 59980 && p->dim <= 60020, "60 s dimmed");
    EXPECT(p->off >= 599950 && p->off <= 600050, "10 min off, kept while unlinked");
    EXPECT(p->lk >= 89980 && p->lk <= 90100, "linked only before the link went");
    EXPECT(p->ble >= 89980 && p->ble <= 90100, "BLE connected only before the link went");
    EXPECT(p->itvl == 45000, "the interval in force at the report");
}

static void test_old_daemon(void)
{
    s_welcomed = true;
    s_minor = 18;
    boot();
    preport_on_link(true, s_now);
    run(2 * PREPORT_EVERY_MS, 20);
    EXPECT(s_n_sent == 0, "a 0.18 daemon gets no report");
    s_minor = 19;
}

static void test_deep(void)
{
    /* before DEEP: figures to RTC memory; a refused DEEP takes them back */
    s_welcomed = false;
    s_display_on = false;
    s_wake = HAL_WAKE_POWER_ON;
    memset(s_words, 0, sizeof s_words);
    boot();
    run(600000, 50);
    preport_save(s_now);
    preport_unsave();
    preport_times_t t;
    uint32_t seq;
    EXPECT(!preport_unpack(s_words, &t, &seq), "a refused DEEP leaves no figures in RTC memory");
    run(1000, 50);
    preport_save(s_now);
    EXPECT(preport_unpack(s_words, &t, &seq) && t.off >= 600950, "the unsent figures are saved");

    /* the next boot ends that DEEP after 2 h */
    s_wake = HAL_WAKE_BUTTON;
    s_slept_ms = 7200000;
    boot();
    EXPECT(preport_times()->off >= 600950 && preport_times()->deep == 7200000 && preport_times()->nd == 1,
           "kept figures plus the sleep");
    EXPECT(!preport_unpack(s_words, &t, &seq), "used once");
    s_welcomed = true;
    preport_on_link(true, s_now);
    run(20, 20);
    EXPECT(s_n_sent == 1 && s_sent[0].deep == 7200000 && s_sent[0].nd == 1, "the report after the wake carries it");

    /* a power-on: RTC words are ignored even if something is there */
    preport_pack(&(preport_times_t){.on = 5}, 9, s_words);
    s_wake = HAL_WAKE_POWER_ON;
    boot();
    EXPECT(preport_times()->on == 0 && preport_seq() == 0, "a power-on starts from zero");
}

static void test_battery_sampled(void)
{
    s_welcomed = false;
    s_display_on = false;
    boot();
    uint32_t before = s_battery_reads;
    run(10 * 60000, 50);
    EXPECT(s_battery_reads - before >= 10 && s_battery_reads - before <= 11,
           "the battery is read once a minute when nothing else does (Battery Level notify)");
}

int main(void)
{
    test_account_pure();
    test_pack_unpack();
    test_cadence();
    test_states_and_unlinked();
    test_old_daemon();
    test_deep();
    test_battery_sampled();
    if (failures) return 1;
    printf("power_report: all passed\n");
    return 0;
}
