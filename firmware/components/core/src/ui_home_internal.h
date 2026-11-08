/* Shared by the deck's two files only: ui_home.c (slots, selection, actions, timers) and
 * ui_home_card.c (the centre card, the key badges and the link row). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "model.h"
#include "ui_internal.h"

typedef struct {
    uint32_t sid;
    uint8_t n_merged; /* > 0: this slot stands for that many Done sessions */
} home_slot_t;

/* ── deck state, read by the card (ui_home.c) ── */
const session_t *ui_home_sel_session(void);
const home_slot_t *ui_home_sel_slot(void); /* NULL when there are no slots */
int ui_home_slot_count(void);
/* An unanswered permission or question for the session: what ASK would show. */
const attention_t *ui_home_askable_of(const session_t *s);

/* ── the card (ui_home_card.c) ── */
void ui_home_card_create(lv_obj_t *centre);
void ui_home_card_update(void);          /* redraw from the model and the selection */
void ui_home_card_tick(void);            /* the 500 ms clock: time in state, stale age, quiet glyph */
void ui_home_card_set_ambient(bool ambient);
void ui_home_card_report(ui_report_t *r);
