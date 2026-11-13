/* Power report (docs/design/POWER.md, proto 0.19). The device cannot measure current; it reports what it can
 * (the battery and input voltages) and where its time went, and the daemon fits a drain rate per state. */
#include "power_report.h"

#include "debug.h"
#include "hal.h"
#include "link.h"
#include "power.h"
#include "ui.h"

/* RTC word layout (HAL_RETAINED_WORDS >= W_N): a magic so a power-on's zeros are never read as figures. */
enum { W_MAGIC, W_SEQ, W_ON, W_DIM, W_OFF, W_DEEP, W_LK, W_BLE, W_ND, W_N };
#define PREPORT_MAGIC 0x50575231u /* "PWR1" */
_Static_assert(W_N <= HAL_RETAINED_WORDS, "power report figures do not fit the RTC words");

static preport_times_t s_t;
static uint32_t s_seq;        /* reports sent since power-on */
static uint32_t s_last_ms;    /* the last tick: time since then is accounted at the next */
static uint32_t s_next_ms;    /* the next report is due */
static uint32_t s_sample_ms;  /* the next battery read */
static bool s_due;            /* s_next_ms is set (a daemon welcomed us) */

static uint32_t sat_add(uint32_t a, uint32_t b) { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }

void preport_account(preport_times_t *t, preport_state_t st, bool linked, bool ble, uint32_t dt_ms)
{
    uint32_t *slot = st == PREPORT_ON ? &t->on : st == PREPORT_DIM ? &t->dim : &t->off;
    *slot = sat_add(*slot, dt_ms);
    if (linked) t->lk = sat_add(t->lk, dt_ms);
    if (ble) t->ble = sat_add(t->ble, dt_ms);
}

void preport_add_deep(preport_times_t *t, uint32_t slept_ms)
{
    t->deep = sat_add(t->deep, slept_ms);
    t->nd = sat_add(t->nd, 1);
}

void preport_pack(const preport_times_t *t, uint32_t seq, uint32_t *w)
{
    w[W_SEQ] = seq;
    w[W_ON] = t->on;
    w[W_DIM] = t->dim;
    w[W_OFF] = t->off;
    w[W_DEEP] = t->deep;
    w[W_LK] = t->lk;
    w[W_BLE] = t->ble;
    w[W_ND] = t->nd;
    w[W_MAGIC] = PREPORT_MAGIC;
}

bool preport_unpack(const uint32_t *w, preport_times_t *t, uint32_t *seq)
{
    if (w[W_MAGIC] != PREPORT_MAGIC) return false;
    *seq = w[W_SEQ];
    *t = (preport_times_t){.on = w[W_ON], .dim = w[W_DIM], .off = w[W_OFF], .deep = w[W_DEEP],
                           .lk = w[W_LK], .ble = w[W_BLE], .nd = w[W_ND]};
    return true;
}

const preport_times_t *preport_times(void) { return &s_t; }
uint32_t preport_seq(void) { return s_seq; }

void preport_init(uint32_t now_ms)
{
    s_t = (preport_times_t){0};
    s_seq = 0;
    uint32_t *w = hal_retained_words();
    if (hal_wake_reason() != HAL_WAKE_POWER_ON) {
        /* A deep sleep ended: what it kept, then the sleep itself. */
        bool kept = preport_unpack(w, &s_t, &s_seq);
        preport_add_deep(&s_t, hal_deep_slept_ms());
        dbg_event("power", "report_restored", "\"kept\":%s,\"seq\":%u,\"deep\":%u", kept ? "true" : "false",
                  (unsigned)s_seq, (unsigned)s_t.deep);
    }
    w[W_MAGIC] = 0; /* used once: a later reset must not count them again */
    s_last_ms = now_ms;
    s_sample_ms = now_ms;
    s_due = false;
}

void preport_on_link(bool welcomed, uint32_t now_ms)
{
    s_due = welcomed;
    s_next_ms = now_ms; /* a new link hears where the time went at once */
}

static preport_state_t state_now(void)
{
    if (!power_display_on()) return PREPORT_OFF;
    return ui_dimmed() ? PREPORT_DIM : PREPORT_ON;
}

/* Time since the last tick goes to the state that held during it (this runs first in the core step). */
static void account(uint32_t now_ms)
{
    uint32_t dt = now_ms - s_last_ms;
    s_last_ms = now_ms;
    preport_account(&s_t, state_now(), link_welcomed(), hal_link_ble_interval_us() != 0, dt);
}

static bool send(void)
{
    hal_battery_t b = hal_battery_read();
    hal_battery_mv_t v = hal_battery_mv();
    link_power_t p = {.mv = v.mv, .mvf = v.mv_filt, .vin = v.vin_mv, .pct = b.percent, .chg = b.charging,
                      .usb = b.usb_present, .on = s_t.on, .dim = s_t.dim, .off = s_t.off, .deep = s_t.deep,
                      .lk = s_t.lk, .ble = s_t.ble, .itvl = hal_link_ble_interval_us(), .seq = s_seq, .nd = s_t.nd};
    bool ok = link_send_power(&p);
    dbg_event("power", "report",
              "\"sent\":%s,\"seq\":%u,\"mv\":%u,\"on\":%u,\"dim\":%u,\"off\":%u,\"deep\":%u,\"lk\":%u,\"ble\":%u,"
              "\"itvl\":%u,\"nd\":%u",
              ok ? "true" : "false", (unsigned)p.seq, (unsigned)p.mv, (unsigned)p.on, (unsigned)p.dim,
              (unsigned)p.off, (unsigned)p.deep, (unsigned)p.lk, (unsigned)p.ble, (unsigned)p.itvl, (unsigned)p.nd);
    if (!ok) return false;
    s_t = (preport_times_t){0};
    s_seq++;
    return true;
}

void preport_tick(uint32_t now_ms)
{
    account(now_ms);
    if ((int32_t)(now_ms - s_sample_ms) >= 0) {
        s_sample_ms = now_ms + PREPORT_SAMPLE_MS;
        (void)hal_battery_read(); /* the board notifies BLE Battery Level on a 1 % change */
    }
    if (!s_due || !link_welcomed() || model_peer_minor() < LINK_POWER_MINOR) return;
    if ((int32_t)(now_ms - s_next_ms) < 0) return;
    s_next_ms = now_ms + (send() ? PREPORT_EVERY_MS : PREPORT_RETRY_MS);
}

void preport_save(uint32_t now_ms)
{
    account(now_ms);
    preport_pack(&s_t, s_seq, hal_retained_words());
}

void preport_unsave(void) { hal_retained_words()[W_MAGIC] = 0; }
