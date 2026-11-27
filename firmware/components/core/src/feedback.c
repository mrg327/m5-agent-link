#include "feedback.h"

#include "hal.h"
#include "motion.h"
#include "persona.h"
#include "prefs.h"
#include "sound.h"
#include "toy_gate.h"

#define DETENT_MIN_GAP_MS 45

/* ── toys (EXPERIMENTS.md "Toys"): what a toy plays, and the gate that puts every alert first ── */

static toy_gate_t s_toy_gate;
static bool s_toy_mic;                                 /* a toy started the microphone */
static uint64_t s_toy_sound_end_us, s_toy_buzz_end_us; /* what a toy plays runs until then */

static uint32_t now_ms(void) { return (uint32_t)(hal_time_us() / 1000); }

/* The fun word (persona.h): with the toys bit off every toy stays still. A toy is started by the
 * user, so only the bit decides here; the gate below does the rest. */
static bool toys_off(void)
{
    persona_ctx_t c = {.fun = prefs_fun(), .display_on = true, .user_started = true};
    return persona_allow(PF_TOYS, &c) == 0;
}

static toy_gate_in_t toy_in(void)
{
    return (toy_gate_in_t){
        .alert = model_settings()->alert,
        .face_down = motion_face_down(),
        .volume = prefs_volume(),
        .mic_on = s_toy_mic,
        .fun_off = toys_off(),
    };
}

static uint32_t pattern_ms(const uint16_t *p, size_t n)
{
    uint32_t ms = 0;
    for (size_t i = 0; i < n; i++) ms += p[i];
    return ms;
}

/* The cue a role plays, in ms (0 for none). */
static uint32_t cue_ms(const char *role)
{
    const snd_role_t *r = role ? snd_role(role) : NULL;
    const snd_sound_t *s = role ? snd_for(role) : NULL;
    return r && s ? (uint32_t)(snd_role_length(r, s) * 1000u / SND_RATE) : 0;
}

/* An alert buzzes or sounds next: the toy's microphone stops first (the speaker is silent while it
 * records, so the cue would be lost), then the toy's own sound and buzz, and toys stay quiet until
 * the cue and the buzz have played. */
static void toy_yield(const uint16_t *p, size_t n, const char *role)
{
    uint64_t now = hal_time_us();
    if (s_toy_mic) {
        hal_mic_stop();
        s_toy_mic = false;
    }
    if (s_toy_sound_end_us > now) hal_audio_play(NULL, 0);
    if (s_toy_buzz_end_us > now) hal_haptic_play(NULL, 0);
    s_toy_sound_end_us = s_toy_buzz_end_us = 0;
    uint32_t buzz = pattern_ms(p, n), cue = cue_ms(role);
    toy_gate_alert(&s_toy_gate, now_ms(), buzz > cue ? buzz : cue);
}

bool feedback_toy_buzz(const uint16_t *pattern, size_t n)
{
    toy_gate_in_t in = toy_in();
    if (!n || !toy_gate_may_buzz(&s_toy_gate, &in, now_ms())) return false;
    hal_haptic_play(pattern, n);
    s_toy_buzz_end_us = hal_time_us() + 1000ull * pattern_ms(pattern, n);
    return true;
}

bool feedback_toy_detent(void)
{
    toy_gate_in_t in = toy_in();
    if (!toy_gate_may_buzz(&s_toy_gate, &in, now_ms())) return false;
    feedback_detent();
    return true;
}

bool feedback_toy_play(const int16_t *pcm, size_t n, uint8_t volume_pct)
{
    toy_gate_in_t in = toy_in();
    if (!pcm || !n || !hal_board_info()->has_speaker ||
        !toy_gate_may_sound(&s_toy_gate, &in, now_ms())) {
        return false;
    }
    /* never louder than the stored level; the next alert cue sets that level again */
    uint8_t stored = prefs_volume_percent();
    hal_audio_set_volume(volume_pct < stored ? volume_pct : stored);
    hal_audio_play(pcm, n);
    s_toy_sound_end_us = hal_time_us() + (uint64_t)n * 1000000u / SND_RATE;
    return true;
}

bool feedback_toy_mic(bool on)
{
    if (!on) {
        if (s_toy_mic) hal_mic_stop();
        s_toy_mic = false;
        return false;
    }
    if (s_toy_mic) return true;
    if (toy_gate_holding(&s_toy_gate, now_ms()) || !hal_mic_start()) return false;
    s_toy_mic = true;
    return true;
}

void feedback_toy_interrupt(void)
{
    uint64_t now = hal_time_us();
    if (s_toy_sound_end_us > now) hal_audio_play(NULL, 0);
    if (s_toy_buzz_end_us > now) hal_haptic_play(NULL, 0);
    s_toy_sound_end_us = s_toy_buzz_end_us = 0;
    feedback_toy_mic(false);
}

/* A watch lying face down is silent (MOTION.md): nothing buzzes or sounds until it is turned back.
 * The user's alert mode is not touched, so it applies again the moment the watch is lifted. */
static void haptic(const uint16_t *p, size_t n)
{
    if (model_settings()->alert != ALERT_SILENT && !motion_face_down()) hal_haptic_play(p, n);
}

/* Speaker cues (roles in sound_bank.c); only in the full alert mode ("Sounds: on"), at the stored
 * volume. */
static void sound(const char *role)
{
    if (model_settings()->alert != ALERT_FULL || motion_face_down() || !prefs_volume()) return;
    hal_audio_set_volume(prefs_volume_percent());
    sound_play(role);
}

/* Entry haptics follow docs/design/UI-RING.md §4. Normal pulses stay >= 35 ms: shorter ones do not
 * spin the motor. */
void feedback_attention(att_kind_t kind)
{
    static const uint16_t permission[] = {250, 120, 250};
    static const uint16_t question[] = {40, 90, 40};
    static const uint16_t error[] = {500};
    static const uint16_t done[] = {35};
    switch (kind) {
    case ATT_APPROVAL: toy_yield(permission, 3, "input"); break;
    case ATT_QUESTION: toy_yield(question, 3, "input"); break;
    case ATT_ERROR: toy_yield(error, 1, NULL); break;
    case ATT_DONE: toy_yield(done, 1, "done"); break;
    }
    switch (kind) {
    case ATT_APPROVAL:
        haptic(permission, 3);
        sound("input");
        break;
    case ATT_QUESTION:
        haptic(question, 3);
        sound("input");
        break;
    case ATT_ERROR: haptic(error, 1); break;
    case ATT_DONE:
        haptic(done, 1);
        sound("done");
        break;
    }
}

/* Reminders grow louder (issue 20261126-a909): a soft pulse first, for a user who is already
 * reaching; then the kind's own pattern; last the pattern twice, 400 ms apart, with the sound, so
 * the last one stands out even with the sound off. */
void feedback_reminder(att_kind_t kind, int level)
{
    static const uint16_t soft[] = {60};
    static const uint16_t perm_twice[] = {250, 120, 250, 400, 250, 120, 250};
    static const uint16_t ask_twice[] = {40, 90, 40, 400, 40, 90, 40};
    bool q = kind == ATT_QUESTION;
    if (level <= 1) {
        haptic(soft, 1);
    } else if (level == 2) {
        static const uint16_t perm_once[] = {250, 120, 250}; /* the arrival patterns */
        static const uint16_t ask_once[] = {40, 90, 40};
        haptic(q ? ask_once : perm_once, 3);
    } else {
        haptic(q ? ask_twice : perm_twice, 7);
        sound("input");
    }
}

void feedback_tick(void)
{
    static const uint16_t tick[] = {35};
    haptic(tick, 1);
}

/* The board plays a pulse of at most 15 ms at the 100 % kick level only, which feels like a
 * click. */
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

/* Pulses shrink and the gaps grow: the opposite shape, so a deny hold feels different in the
 * hand. */
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

/* The watch just booted a fresh OTA image and the daemon welcomed it: the "updated" flourish (no
 * haptic). Called once per update (ota.c), never on a power-on or a USB flash. */
void feedback_updated(void) { sound("updated"); }

/* Short then long: an answer went out that neither grants nor denies. Two pulses, so it differs
 * from the reject (three) by count, and from denied (two equal) by its rising rhythm. */
void feedback_reply_sent(void)
{
    static const uint16_t sent[] = {35, 80, 120};
    haptic(sent, 3);
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

/* Quiet mode toggled (UI-V2 §5): two equal spaced pulses, unlike the tick (one), the sent buzz
 * (short then long) and the reject (three). */
void feedback_quiet_toggled(void)
{
    static const uint16_t quiet[] = {35, 100, 35};
    haptic(quiet, 3);
}

/* Volume stepped on STATUS: the cue heard most (an agent asking) at the new level, so the user
 * hears what alerts will sound like; the tick confirms even where the speaker is held back (face
 * down). */
void feedback_volume_preview(void)
{
    feedback_tick();
    sound("input");
}

/* A session finished (UI-V2 §5): done is two soft pulses with a long gap, error three firm
 * pulses. */
void feedback_finished(bool error)
{
    static const uint16_t done[] = {35, 200, 35};
    static const uint16_t fail[] = {120, 90, 120, 90, 120};
    if (error) {
        toy_yield(fail, 5, NULL);
    } else {
        toy_yield(done, 3, "done");
    }
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
    toy_yield(warn, 1, NULL);
    haptic(warn, 1);
}

void feedback_hello(void)
{
    static const uint16_t hello[] = {60};
    /* unconditional: proves the motor path regardless of the alert mode */
    hal_haptic_play(hello, 1);
}

/* A palm cover: whatever buzz or sound is running stops. Nothing is stored; the next alert plays
 * normally. */
void feedback_silence_now(void)
{
    hal_haptic_play(NULL, 0);
    hal_audio_play(NULL, 0);
}
