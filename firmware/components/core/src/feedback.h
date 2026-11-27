/* Haptic and sound feedback; honours the alert mode (full: haptic + sound, quiet: haptic, silent:
 * none). */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "model.h"

void feedback_attention(att_kind_t kind);
/* Reminder `level` (1 to 3, nag.h) of an unanswered request of this kind: one soft pulse; the
 * arrival pattern; the pattern twice and the sound. */
void feedback_reminder(att_kind_t kind, int level);
void feedback_tick(void);       /* selection change by button, command sent */
void feedback_detent(void);     /* one rim-scrub segment crossed: 15 ms kick only, rate limited */
void feedback_hold_ramp(void);  /* approve hold started: escalating pulses over ~1 s */
void feedback_deny_ramp(void);  /* deny hold started: falling pulses over ~0.6 s */
void feedback_reply_sent(void); /* an answer went out: short then long */
void feedback_hold_stop(void);  /* approve hold released early */
void feedback_approved(void);   /* approve sent: one long pulse */
void feedback_updated(void); /* first welcome after an OTA update: the "updated" tone, no haptic */
void feedback_denied(void);  /* deny sent: two medium pulses */
void feedback_reject(void);  /* command could not be sent: three short pulses */
void feedback_scroll_stop(void);   /* SCROLL reached the bottom or the top: one 40 ms thud */
void feedback_quiet_toggled(void); /* STATUS: quiet mode changed: two spaced pulses */
/* STATUS: volume stepped: a tick and the "input" cue at the new level */
void feedback_volume_preview(void);
/* a watched or notify-armed session finished: done two soft, error three firm pulses */
void feedback_finished(bool error);
void feedback_usage_warn(void); /* the 5-hour window crossed 80 % (GAUGES.md): one 200 ms buzz */
void feedback_hello(void);      /* boot buzz; ignores the alert mode */
/* stop the motor and the speaker at once (palm cover); changes no setting */
void feedback_silence_now(void);

/* Toys (EXPERIMENTS.md "Toys"). Every alert above (an arrival, the nag, a finish, the usage
 * warning) first stops a toy's microphone, sound and buzz, then holds toys quiet while its cue and
 * buzz play (toy_gate.h). The toy calls return false when the gate refuses. */
bool feedback_toy_buzz(const uint16_t *pattern, size_t n); /* not in silent mode, nor face down */
bool feedback_toy_detent(void);                            /* feedback_detent, gated as a buzz */
/* pcm stays valid until played (sound_toy_pcm); at most the stored level: volume_pct is capped */
bool feedback_toy_play(const int16_t *pcm, size_t n, uint8_t volume_pct);
bool feedback_toy_mic(bool on);    /* true: the toy's microphone runs (hal_mic_read reads it) */
void feedback_toy_interrupt(void); /* a toy closes: its sound, buzz and microphone stop */
