/* Haptic and sound feedback; honours the alert mode (full: haptic + sound, quiet: haptic, silent: none). */
#pragma once
#include "model.h"

void feedback_attention(att_kind_t kind);
void feedback_tick(void);    /* selection change by button, command sent */
void feedback_detent(void);  /* one rim-scrub segment crossed: 15 ms kick only, rate limited */
void feedback_hold_ramp(void); /* approve hold started: escalating pulses over ~1 s */
void feedback_hold_stop(void); /* approve hold released early */
void feedback_confirm(void); /* command accepted */
void feedback_reject(void);  /* command could not be sent */
void feedback_hello(void);   /* boot buzz; ignores the alert mode */
