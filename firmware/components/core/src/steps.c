#include "steps.h"

#include <string.h>

/* Copies at most STEP_TEXT_MAX - 1 bytes without cutting a UTF-8 sequence in half. */
static void set_text(step_t *st, const char *text)
{
    size_t n = text ? strlen(text) : 0;
    if (n >= STEP_TEXT_MAX) {
        n = STEP_TEXT_MAX - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(st->text, text, n);
    st->text[n] = 0;
}

void steps_clear(steps_t *t) { memset(t, 0, sizeof *t); }

static step_t *newest(steps_t *t) { return t->count ? &t->s[t->count - 1] : NULL; }

void steps_update(steps_t *t, bool has_n, uint32_t n, bool has_ok, bool ok, const char *text)
{
    if (has_n) {
        if (n < t->last_n || n == 0) steps_clear(t); /* a new turn; a nonzero n then starts it */
        if (n == 0) return;
        if (n > t->last_n) {
            if (t->count == STEPS_MAX) {
                memmove(&t->s[0], &t->s[1], (STEPS_MAX - 1) * sizeof t->s[0]);
                t->count--;
            }
            step_t *st = &t->s[t->count++];
            memset(st, 0, sizeof *st);
            st->n = n;
            st->state = STEP_RUNNING;
            set_text(st, text);
            t->last_n = n;
            text = NULL;
        }
    }
    step_t *cur = newest(t);
    if (cur && cur->n == t->last_n) {
        if (text && text[0] && cur->state == STEP_RUNNING) set_text(cur, text);
        if (has_ok) cur->state = ok ? STEP_OK : STEP_FAIL;
    }
}

void steps_seed(steps_t *t, uint32_t n, step_state_t state, const char *text)
{
    steps_clear(t);
    if (n == 0) return;
    t->last_n = n;
    t->count = 1;
    t->s[0].n = n;
    t->s[0].state = (uint8_t)state;
    set_text(&t->s[0], text);
}

const step_t *steps_find(const steps_t *t, uint32_t n)
{
    for (int i = 0; i < t->count; i++)
        if (t->s[i].n == n) return &t->s[i];
    return NULL;
}
