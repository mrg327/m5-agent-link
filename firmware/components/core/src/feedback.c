#include "feedback.h"

#include "hal.h"

static void haptic(const uint16_t *p, size_t n)
{
    if (model_settings()->alert != ALERT_SILENT) hal_haptic_play(p, n);
}

static void sound(hal_sound_t s)
{
    if (model_settings()->alert == ALERT_FULL) hal_sound_play(s);
}

void feedback_attention(att_kind_t kind)
{
    static const uint16_t needs_input[] = {250, 120, 250};
    static const uint16_t error[] = {80, 70, 80, 70, 80};
    static const uint16_t done[] = {80};
    switch (kind) {
    case ATT_APPROVAL:
    case ATT_QUESTION: haptic(needs_input, 3); sound(HAL_SOUND_CHIME_INPUT); break;
    case ATT_ERROR: haptic(error, 5); sound(HAL_SOUND_TONE_ERROR); break;
    case ATT_DONE: haptic(done, 1); sound(HAL_SOUND_TICK_DONE); break;
    }
}

void feedback_tick(void)
{
    static const uint16_t tick[] = {35};
    haptic(tick, 1);
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
