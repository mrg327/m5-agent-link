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
    static const uint16_t error[] = {70, 60, 70, 60, 70};
    static const uint16_t done[] = {70};
    switch (kind) {
    case ATT_APPROVAL:
    case ATT_QUESTION: haptic(needs_input, 3); sound(HAL_SOUND_CHIME_INPUT); break;
    case ATT_ERROR: haptic(error, 5); sound(HAL_SOUND_TONE_ERROR); break;
    case ATT_DONE: haptic(done, 1); sound(HAL_SOUND_TICK_DONE); break;
    }
}

void feedback_tick(void)
{
    static const uint16_t tick[] = {12};
    haptic(tick, 1);
}

void feedback_confirm(void)
{
    static const uint16_t ok[] = {40, 50, 40};
    haptic(ok, 3);
}
