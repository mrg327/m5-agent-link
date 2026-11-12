/* Haptic and sound feedback; honours the alert mode (full: haptic + sound, quiet: haptic, silent: none). */
#pragma once
#include "model.h"

void feedback_attention(att_kind_t kind);
void feedback_tick(void);    /* selection change by button, command sent */
void feedback_detent(void);  /* one rim-scrub segment crossed: 15 ms kick only, rate limited */
void feedback_hold_ramp(void); /* approve hold started: escalating pulses over ~1 s */
void feedback_deny_ramp(void); /* deny hold started: falling pulses over ~0.6 s */
void feedback_reply_sent(void); /* a question option went out: three ticks */
void feedback_hold_stop(void); /* approve hold released early */
void feedback_approved(void); /* approve sent: one long pulse */
void feedback_denied(void);   /* deny sent: two medium pulses */
void feedback_reject(void);   /* command could not be sent: three short pulses */
void feedback_scroll_stop(void); /* SCROLL reached the bottom or the top: one 40 ms thud */
void feedback_quiet_toggled(void); /* STATUS: quiet mode changed: two spaced pulses */
void feedback_finished(bool error); /* a watched or notify-armed session finished: done two soft, error three firm pulses */
void feedback_usage_warn(void); /* the 5-hour window crossed 80 % (GAUGES.md): one 200 ms buzz */
void feedback_hello(void);   /* boot buzz; ignores the alert mode */
void feedback_silence_now(void); /* stop the motor and the speaker at once (palm cover); changes no setting */
