#include "persona.h"

uint32_t persona_bit(persona_feat_t f) { return f < PF_COUNT ? 1u << (unsigned)f : 0; }

unsigned persona_allow(persona_feat_t f, const persona_ctx_t *c)
{
    if (!(c->fun & persona_bit(f)) || !c->display_on) return 0;
    unsigned a = PA_POSE | PA_ONESHOT | PA_LOOP | PA_HAPTIC | PA_SOUND;
    const unsigned voice = PA_HAPTIC | PA_SOUND;
    /* guardrail 1: alerts always win. A finish keeps its own buzz and sound and loses its
     * picture; the asking mark keeps its gesture; everything else holds still. */
    if (c->needs_you) {
        unsigned keep = f == PF_CELEBRATE ? voice
                        : c->alerting     ? (unsigned)(PA_POSE | PA_ONESHOT)
                                          : (unsigned)PA_POSE;
        a &= keep;
    }
    if (c->face_down || c->alert >= 2 || c->hush) a &= ~voice;
    if (c->alert == 1 || c->quiet) {
        a &= ~(unsigned)PA_SOUND;
        if (!c->user_started && !c->alerting) a &= ~(unsigned)PA_HAPTIC;
    }
    if (!c->usb) a &= ~(unsigned)PA_LOOP; /* guardrail 6: dock-first power */
    if (c->fun & FUN_REDUCED) a &= ~(unsigned)(PA_LOOP | PA_ONESHOT);
    /* guardrail 2: fun never initiates a buzz or a sound */
    if (!c->user_started && !c->alerting) a &= ~voice;
    return a;
}
