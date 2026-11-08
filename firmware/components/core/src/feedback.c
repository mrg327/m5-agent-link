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

/* Pulses shrink and the gaps grow: the opposite shape, so a deny hold feels different in the hand. */
void feedback_deny_ramp(void)
{
    static const uint16_t deny_ramp[] = {100, 50, 80, 65, 65, 85, 55, 110, 45};
    haptic(deny_ramp, sizeof deny_ramp / sizeof deny_ramp[0]);
}

void feedback_hold_stop(void)
{
    if (model_settings()->alert != ALERT_SILENT) hal_haptic_play(NULL, 0);
}

/* Approve, deny and reject differ in count and length, so they can be told apart without looking:
 * one long pulse, two medium, three short. */
void feedback_approved(void)
{
    static const uint16_t approved[] = {150};
    haptic(approved, 1);
}

void feedback_denied(void)
{
    static const uint16_t denied[] = {60, 90, 60};
    haptic(denied, 3);
}

/* Three ticks: an answer went out that neither grants nor denies. */
void feedback_reply_sent(void)
{
    static const uint16_t reply[] = {35, 60, 35, 60, 35};
    haptic(reply, 5);
}

void feedback_reject(void)
{
    static const uint16_t no[] = {35, 70, 35, 70, 35};
    haptic(no, 5);
}

void feedback_hello(void)
{
    static const uint16_t hello[] = {60};
    hal_haptic_play(hello, 1); /* unconditional: proves the motor path regardless of the alert mode */
}
