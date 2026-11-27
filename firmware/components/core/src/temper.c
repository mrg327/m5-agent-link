#include "temper.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "persona.h"
#include "sha256.h"

#define LOOP_LEVELS 8 /* cos8: a breath has 16 level changes a period */
#define LEAN_PX 3
#define LEAN_MS 200
#define DIP_MS 90
#define SHIVER_STEP_MS 40
#define ASLEEP_LUM 60
#define ASLEEP_TURN 6
#define IDLE_LUM_LOW 80

const temper_pose_t temper_neutral = {.lum = 100};

bool temper_key(const char *name, uint32_t sid, char *key, size_t n)
{
    key[0] = 0;
    if (name && name[0]) {
        snprintf(key, n, "%s", name);
        return true;
    }
    if (!sid) return false;
    snprintf(key, n, "\x01sid %u", (unsigned)sid);
    return true;
}

void temper_hash(const char *name, uint32_t sid, uint8_t t[TEMPER_HASH])
{
    char key[96];
    uint8_t out[32];
    temper_key(name, sid, key, sizeof key);
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)key, strlen(key));
    sha256_final(&c, out);
    memcpy(t, out + 10, TEMPER_HASH);
}

temper_t temper_params(const uint8_t t[TEMPER_HASH])
{
    temper_t p;
    /* 3.0 to 6.0 s in steps of 250 ms */
    unsigned span = TEMPER_PERIOD_MAX - TEMPER_PERIOD_MIN;
    p.period_ms = (uint16_t)(TEMPER_PERIOD_MIN + (t[5] * span / 255 + 125) / 250 * 250);
    if (p.period_ms > TEMPER_PERIOD_MAX) p.period_ms = TEMPER_PERIOD_MAX;
    p.sway_deg = (uint8_t)(TEMPER_SWAY_MIN + t[6] % (TEMPER_SWAY_MAX - TEMPER_SWAY_MIN + 1));
    p.sway_period_ms = (uint16_t)(t[7] & 1 ? p.period_ms * 3 / 2 : p.period_ms * 2);
    p.dir = (int8_t)(t[7] & 2 ? 1 : -1);
    p.phase_ms = (uint16_t)(t[8] * p.period_ms / 256);
    p.spark_deg = (uint8_t)(t[9] % 45);
    return p;
}

temper_t temper_of(const char *name, uint32_t sid)
{
    uint8_t t[TEMPER_HASH];
    temper_hash(name, sid, t);
    return temper_params(t);
}

static float out3(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

/* cos8 at time ms of a loop of period p: 0 (low) .. LOOP_LEVELS - 1 (high), high at phase 0 */
static int cos8(uint32_t ms, uint32_t p)
{
    float c = cosf(6.2831853f * (float)(ms % p) / (float)p);
    return (int)lroundf((float)(LOOP_LEVELS - 1) * (1.0f + c) / 2.0f);
}

/* The breath: brightness and (working) the middle ring's sway. Time is sampled in sixteenths of
 * the period, so the mark changes at most 16 times a breath: one redraw carries both channels. */
static void breathe(const temper_t *tp, bool idle, uint32_t now_ms, temper_pose_t *p)
{
    uint32_t period = idle ? 2u * tp->period_ms : tp->period_ms;
    uint32_t step = period / 16u;
    uint32_t t = (now_ms + tp->phase_ms) / step * step;
    int lv = cos8(t, period);
    if (idle) {
        p->lum = (uint8_t)(IDLE_LUM_LOW + (100 - IDLE_LUM_LOW) * lv / (LOOP_LEVELS - 1));
        return;
    }
    p->lum = (uint8_t)(TEMPER_LUM_LOW + (100 - TEMPER_LUM_LOW) * lv / (LOOP_LEVELS - 1));
    float s = sinf(6.2831853f * (float)(t % tp->sway_period_ms) / (float)tp->sway_period_ms);
    p->rot_mid = (int16_t)lroundf((float)(tp->dir * tp->sway_deg) * s);
}

static int8_t dip(uint32_t since_ms, int r)
{
    int down = (int)lroundf((float)r * 0.10f), up = (int)lroundf((float)r * 0.06f);
    if (up > TEMPER_REACH) up = TEMPER_REACH;
    if (since_ms < DIP_MS) return (int8_t)-down;
    if (since_ms < 2 * DIP_MS) return (int8_t)up;
    if (since_ms >= TEMPER_GESTURE_MS) return 0;
    float t = (float)(since_ms - 2 * DIP_MS) / (float)(TEMPER_GESTURE_MS - 2 * DIP_MS);
    return (int8_t)lroundf((float)up * (1.0f - out3(t)));
}

static int8_t shiver(const temper_t *tp, uint32_t since_ms, int amp)
{
    static const int8_t s_steps[] = {2, -2, 2, -1, 1, 0};
    uint32_t i = since_ms / SHIVER_STEP_MS;
    if (i >= sizeof s_steps) return 0;
    return (int8_t)(tp->dir * s_steps[i] * amp / 2);
}

temper_pose_t temper_pose(const temper_t *tp, temper_state_t st, uint32_t since_ms, uint32_t now_ms,
                          unsigned allow, int r)
{
    temper_pose_t p = temper_neutral;
    if (!(allow & PA_POSE)) return p;
    bool once = (allow & PA_ONESHOT) != 0, loop = (allow & PA_LOOP) != 0;
    switch (st) {
    case TEMPER_WORK:
    case TEMPER_IDLE:
        if (loop) breathe(tp, st == TEMPER_IDLE, now_ms, &p);
        break;
    case TEMPER_WANTS: { /* lean, then still: stillness is the signal */
        int lean = LEAN_PX;
        if (once && since_ms < LEAN_MS) {
            lean = 1 + (int)lroundf((float)(LEAN_PX - 1) * out3((float)since_ms / LEAN_MS));
        }
        p.dy = (int8_t)lean;
        break;
    }
    case TEMPER_DONE:
        if (once) p.dr = dip(since_ms, r);
        break;
    case TEMPER_ERROR:
        if (once) p.dx = shiver(tp, since_ms, 2);
        break;
    case TEMPER_ASLEEP:
        p.lum = ASLEEP_LUM;
        p.rot_out = (int16_t)(tp->dir * ASLEEP_TURN);
        break;
    default: break;
    }
    return p;
}

temper_pose_t temper_hero(const temper_t *tp, temper_state_t st, uint32_t since_ms, unsigned allow)
{
    temper_pose_t p = temper_neutral;
    if (!(allow & PA_POSE) || !(allow & PA_ONESHOT)) return p;
    if (st == TEMPER_ERROR) p.dx = shiver(tp, since_ms, 3);
    if (st == TEMPER_DONE) p.dy = (int8_t)-dip(since_ms, 30); /* r 30: down 3, up 2, settle */
    return p;
}

int temper_joy(temper_state_t st, uint32_t since_ms, unsigned allow)
{
    if (st != TEMPER_DONE || !(allow & PA_POSE) || !(allow & PA_ONESHOT)) return -1;
    if (since_ms >= TEMPER_JOY_MS) return -1;
    return (int)(since_ms * 1000u / TEMPER_JOY_MS);
}
