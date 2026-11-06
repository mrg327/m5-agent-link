/* Haptic and sound feedback; honours the alert mode (full: haptic + sound, quiet: haptic, silent: none). */
#pragma once
#include "model.h"

void feedback_attention(att_kind_t kind);
void feedback_tick(void);    /* selection change */
void feedback_confirm(void); /* command accepted */
