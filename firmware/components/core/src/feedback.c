#include "feedback.h"

#include "hal.h"
#include "motion.h"
#include "prefs.h"
#include "sound.h"

#define DETENT_MIN_GAP_MS 45

/* A watch lying face down is silent (MOTION.md): nothing buzzes or sounds until it is turned back. The
 * user's alert mode is not touched, so it applies again the moment the watch is lifted. */
static void haptic(const uint16_t *p, size_t n)
{
    if (model_settings()->alert != ALERT_SILENT && !motion_face_down()) hal_haptic_play(p, n);
}

/* Speaker cues (roles in sound_bank.c); only in the full alert mode ("Sounds: on"), at the stored volume. */
static void sound(const char *role)
{
    if (model_settings()->alert != ALERT_FULL || motion_face_down() || !prefs_volume()) return;
    hal_audio_set_volume(prefs_volume_percent());
    sound_play(role);
}

/* Entry haptics follow docs/design/UI-RING.md §4. Normal pulses stay >= 35 ms: shorter ones do not spin the motor. */
void feedback_attention(att_kind_t kind)
{
    static const uint16_t permission[] = {250, 120, 250};
    static const uint16_t question[] = {40, 90, 40};
    static const uint16_t error[] = {500};
    static const uint16_t done[] = {35};
    switch (kind) {
    case ATT_APPROVAL: haptic(permission, 3); sound("input"); break;
    case ATT_QUESTION: haptic(question, 3); sound("input"); break;
    case ATT_ERROR: haptic(error, 1); break;
    case ATT_DONE: haptic(done, 1); sound("done"); break;
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
    sound("approved");
}

void feedback_denied(void)
{
    static const uint16_t denied[] = {60, 90, 60};
    haptic(denied, 3);
    sound("denied");
}

/* The watch just booted a fresh OTA image and the daemon welcomed it: the "updated" flourish (no haptic).
 * Called once per update (ota.c), never on a power-on or a USB flash. */
void feedback_updated(void)
{
    sound("updated");
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

/* SCROLL hit an end of the scrollback: one thud, firmer than a detent, shorter than a tick pair. */
void feedback_scroll_stop(void)
{
    static const uint16_t stop[] = {40};
    if (model_settings()->alert == ALERT_SILENT) return;
    haptic(stop, 1);
}

/* Quiet mode toggled (UI-V2 §5): two spaced pulses, unlike the tick (one) and the reject (three). */
void feedback_quiet_toggled(void)
{
    static const uint16_t quiet[] = {35, 100, 35};
    haptic(quiet, 3);
}

/* Volume stepped on STATUS: the cue heard most (an agent asking) at the new level, so the user hears what
 * alerts will sound like; the tick confirms even where the speaker is held back (face down). */
void feedback_volume_preview(void)
{
    feedback_tick();
    sound("input");
}

/* A session finished (UI-V2 §5): done is two soft pulses with a long gap, error three firm pulses. */
void feedback_finished(bool error)
{
    static const uint16_t done[] = {30, 200, 30};
    static const uint16_t fail[] = {120, 90, 120, 90, 120};
    if (error) {
        haptic(fail, 5);
    } else {
        haptic(done, 3);
        sound("done");
    }
}

/* One buzz, longer than a tick or an approve, with nothing after it: the plan is running low. */
void feedback_usage_warn(void)
{
    static const uint16_t warn[] = {200};
    haptic(warn, 1);
}

void feedback_hello(void)
{
    static const uint16_t hello[] = {60};
    hal_haptic_play(hello, 1); /* unconditional: proves the motor path regardless of the alert mode */
}

/* A palm cover: whatever buzz or sound is running stops. Nothing is stored; the next alert plays normally. */
void feedback_silence_now(void)
{
    hal_haptic_play(NULL, 0);
    hal_audio_play(NULL, 0);
}
