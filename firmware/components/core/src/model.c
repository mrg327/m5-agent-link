#include "model.h"

#include <string.h>

#include "debug.h"

static session_t s_sessions[MODEL_MAX_SESSIONS];
static bool s_used[MODEL_MAX_SESSIONS];
static attention_t s_att[MODEL_MAX_ATTENTION];
static bool s_att_used[MODEL_MAX_ATTENTION];
static settings_t s_settings;
static uint32_t s_rev;

void model_init(void)
{
    memset(s_used, 0, sizeof s_used);
    memset(s_att_used, 0, sizeof s_att_used);
    memset(&s_settings, 0, sizeof s_settings);
    s_settings.privacy = PRIVACY_PREVIEW;
    s_rev = 1;
}

uint32_t model_rev(void) { return s_rev; }

void model_copy_str(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

const char *sess_state_name(sess_state_t s)
{
    static const char *const names[] = {"start", "work", "input", "error", "done", "idle", "ended"};
    return (unsigned)s < 7 ? names[s] : "?";
}

const char *agent_name(agent_t a)
{
    switch (a) {
    case AGENT_COPILOT: return "copilot";
    case AGENT_CLAUDE: return "claude";
    case AGENT_CLOUD: return "cloud";
    default: return "other";
    }
}

/* ───────── sessions ───────── */

static int find_slot(uint32_t sid)
{
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++)
        if (s_used[i] && s_sessions[i].sid == sid) return i;
    return -1;
}

static void apply(session_t *dst, const session_t *src, uint32_t present)
{
    if (present & SF_AGENT) dst->agent = src->agent;
    if (present & SF_STATE) dst->state = src->state;
    if (present & SF_SUB) dst->sub = src->sub;
    if (present & SF_REPO) memcpy(dst->repo, src->repo, sizeof dst->repo);
    if (present & SF_BRANCH) memcpy(dst->branch, src->branch, sizeof dst->branch);
    if (present & SF_TOOL) memcpy(dst->tool, src->tool, sizeof dst->tool);
    if (present & SF_T0) dst->t0 = src->t0;
    if (present & SF_TST) dst->tst = src->tst;
    if (present & SF_SUMMARY) memcpy(dst->summary, src->summary, sizeof dst->summary);
    if (present & SF_SEEN) dst->seen = src->seen;
}

void model_upsert(const session_t *delta, uint32_t present)
{
    int slot = find_slot(delta->sid);
    if (slot < 0) {
        for (int i = 0; i < MODEL_MAX_SESSIONS && slot < 0; i++)
            if (!s_used[i]) slot = i;
        if (slot < 0) {
            dbg_event_at(DBG_WARN, "model", "session_dropped", "\"sid\":%u,\"why\":\"pool_full\"", (unsigned)delta->sid);
            return;
        }
        memset(&s_sessions[slot], 0, sizeof s_sessions[slot]);
        s_sessions[slot].sid = delta->sid;
        s_sessions[slot].agent = AGENT_OTHER;
        s_sessions[slot].state = SESS_IDLE;
        s_used[slot] = true;
    }
    session_t *dst = &s_sessions[slot];
    sess_state_t before = dst->state;
    apply(dst, delta, present);
    s_rev++;
    dbg_event("model", "session", "\"sid\":%u,\"st\":\"%s\",\"prev\":\"%s\",\"label\":%s",
              (unsigned)dst->sid, sess_state_name(dst->state), sess_state_name(before), dbg_q(dst->repo));
}

void model_replace_all(const session_t *list, size_t n)
{
    memset(s_used, 0, sizeof s_used);
    for (size_t i = 0; i < n && i < MODEL_MAX_SESSIONS; i++) {
        s_sessions[i] = list[i];
        s_used[i] = true;
    }
    for (int a = 0; a < MODEL_MAX_ATTENTION; a++)
        if (s_att_used[a] && find_slot(s_att[a].sid) < 0) s_att_used[a] = false;
    s_rev++;
    dbg_event("model", "snapshot", "\"sessions\":%u", (unsigned)(n > MODEL_MAX_SESSIONS ? MODEL_MAX_SESSIONS : n));
}

void model_remove(uint32_t sid)
{
    int slot = find_slot(sid);
    if (slot < 0) return;
    s_used[slot] = false;
    for (int a = 0; a < MODEL_MAX_ATTENTION; a++)
        if (s_att_used[a] && s_att[a].sid == sid) s_att_used[a] = false;
    s_rev++;
    dbg_event("model", "gone", "\"sid\":%u", (unsigned)sid);
}

size_t model_count(void)
{
    size_t n = 0;
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) n += s_used[i];
    return n;
}

const session_t *model_find(uint32_t sid)
{
    int slot = find_slot(sid);
    return slot < 0 ? NULL : &s_sessions[slot];
}

const session_t *model_at(uint8_t slot) { return (slot < MODEL_MAX_SESSIONS && s_used[slot]) ? &s_sessions[slot] : NULL; }

/* Priority from DESIGN §4: needs_input > error > done(unseen) > working > idle > done(seen). */
static int rank(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return 0;
    case SESS_ERROR: return 1;
    case SESS_DONE: return s->seen ? 5 : 2;
    case SESS_WORK:
    case SESS_START: return 3;
    case SESS_IDLE: return 4;
    default: return 6;
    }
}

static bool before(const session_t *a, const session_t *b)
{
    int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb;
    if (a->tst != b->tst) return a->tst > b->tst;
    return a->sid < b->sid;
}

size_t model_sorted(uint8_t *idx)
{
    size_t n = 0;
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) {
        if (!s_used[i]) continue;
        size_t j = n++;
        while (j > 0 && before(&s_sessions[i], &s_sessions[idx[j - 1]])) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = (uint8_t)i;
    }
    return n;
}

model_counts_t model_counts(void)
{
    model_counts_t c = {0};
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) {
        if (!s_used[i]) continue;
        switch (s_sessions[i].state) {
        case SESS_INPUT: c.waiting++; break;
        case SESS_ERROR: c.error++; break;
        case SESS_WORK:
        case SESS_START: c.running++; break;
        case SESS_DONE: c.done_unseen += !s_sessions[i].seen; break;
        default: break;
        }
    }
    return c;
}

/* ───────── attention ───────── */

void model_attention_add(const attention_t *a)
{
    int slot = -1;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == a->rid) slot = i; /* same request: replace */
    }
    for (int i = 0; i < MODEL_MAX_ATTENTION && slot < 0; i++)
        if (!s_att_used[i]) slot = i;
    if (slot < 0) slot = 0; /* queue full: oldest is least likely to still be live */
    s_att[slot] = *a;
    s_att_used[slot] = true;
    s_rev++;
    dbg_event("model", "attention", "\"rid\":%u,\"sid\":%u,\"kind\":%d", (unsigned)a->rid, (unsigned)a->sid, (int)a->kind);
}

bool model_attention_clear(uint32_t rid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == rid) {
            s_att_used[i] = false;
            s_rev++;
            dbg_event("model", "attention_clear", "\"rid\":%u", (unsigned)rid);
            return true;
        }
    }
    return false;
}

const attention_t *model_attention_find(uint32_t rid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++)
        if (s_att_used[i] && s_att[i].rid == rid) return &s_att[i];
    return NULL;
}

const attention_t *model_attention_for_sid(uint32_t sid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++)
        if (s_att_used[i] && s_att[i].sid == sid) return &s_att[i];
    return NULL;
}

const attention_t *model_attention_first(void)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++)
        if (s_att_used[i]) return &s_att[i];
    return NULL;
}

void model_attention_mark_sent(uint32_t rid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == rid) {
            s_att[i].answer_sent = true;
            s_rev++;
        }
    }
}

void model_attention_expire(uint32_t now_unix)
{
    if (!now_unix) return;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].exp && now_unix > s_att[i].exp) {
            dbg_event("model", "attention_expired", "\"rid\":%u", (unsigned)s_att[i].rid);
            s_att_used[i] = false;
            s_rev++;
        }
    }
}

settings_t *model_settings(void) { return &s_settings; }
