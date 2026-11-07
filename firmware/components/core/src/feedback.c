#include "feedback.h"

#include "hal.h"

#define DETENT_MIN_GAP_MS 45

static void haptic(const uint16_t *p, size_t n)
{
    if (model_settings()->alert != ALERT_SILENT) hal_haptic_play(p, n);
}

static void sound(hal_sound_t s)
{
    if (model_settings()->alert == ALERT_FULL) hal_sound_play(s);
}

/* Entry haptics follow docs/design/UI-RING.md §4. Normal pulses stay >= 35 ms: shorter ones do not spin the motor. */
void feedback_attention(att_kind_t kind)
{
    static const uint16_t permission[] = {250, 120, 250};
    static const uint16_t question[] = {40, 90, 40};
    static const uint16_t error[] = {500};
    static const uint16_t done[] = {35};
    switch (kind) {
    case ATT_APPROVAL: haptic(permission, 3); sound(HAL_SOUND_CHIME_INPUT); break;
    case ATT_QUESTION: haptic(question, 3); sound(HAL_SOUND_CHIME_INPUT); break;
    case ATT_ERROR: haptic(error, 1); sound(HAL_SOUND_TONE_ERROR); break;
    case ATT_DONE: haptic(done, 1); sound(HAL_SOUND_TICK_DONE); break;
    }
}

void feedback_tick(void)
{
    static const uint16_t tick[] = {35};
    haptic(tick, 1);
}

/* The board plays a pulse of at most 15 ms at the 100 % kick level only, which feels like a click. */
void feedback_detent(void)
{
    static const uint16_t detent[] = {15};
    static uint32_t last_ms;
    uint32_t now = (uint32_t)(hal_time_us() / 1000);
    if (last_ms && now - last_ms < DETENT_MIN_GAP_MS) return;
    last_ms = now;
    haptic(detent, 1);
}

/* Pulses grow longer and the gaps shorter; roughly matches the 1.2 s arc fill. */
void feedback_hold_ramp(void)
{
    static const uint16_t ramp[] = {35, 170, 40, 140, 45, 110, 55, 85, 65, 65, 80, 50, 100};
    haptic(ramp, sizeof ramp / sizeof ramp[0]);
}

void feedback_hold_stop(void)
{
    if (model_settings()->alert != ALERT_SILENT) hal_haptic_play(NULL, 0);
}

void feedback_confirm(void)
{
    static const uint16_t ok[] = {45, 50, 45};
    haptic(ok, 3);
}

void feedback_reject(void)
{
    static const uint16_t no[] = {35, 60, 35};
    haptic(no, 3);
}

void feedback_hello(void)
{
    static const uint16_t hello[] = {60};
    hal_haptic_play(hello, 1); /* unconditional: proves the motor path regardless of the alert mode */
}
