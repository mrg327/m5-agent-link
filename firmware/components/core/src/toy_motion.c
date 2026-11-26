#include "toy_motion.h"

#include <stdlib.h>
#include <string.h>

void toy_motion_reset(toy_motion_t *m) { memset(m, 0, sizeof *m); }

void toy_motion_rearm(toy_motion_t *m)
{
    m->armed = false;
    m->still = false;
}

bool toy_motion_feed(toy_motion_t *m, int x, int y, int z, uint32_t now)
{
    if (!m->have) {
        m->have = true;
        m->x = x;
        m->y = y;
        m->z = z;
        return false;
    }
    int dx = x - m->x, dy = y - m->y, dz = z - m->z;
    m->x = x;
    m->y = y;
    m->z = z;
    /* still: the summed change, as motion.c measures it; moved: anything more */
    bool moving = abs(dx) + abs(dy) + abs(dz) > TOY_STILL_MG;
    if (moving) m->armed = true;
    if (moving || z < TOY_FACE_UP_Z) {
        m->still = false;
    } else if (!m->still) {
        m->still = true;
        m->still_from = now;
    }
    /* the vector's length: the same for a flick left or right, whatever the axes' signs */
    int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz;
    if (d2 <= (int64_t)TOY_JOLT_MG * TOY_JOLT_MG) return false;
    if (m->jolted && now - m->jolt_ms < TOY_JOLT_GAP_MS) return false;
    m->jolted = true;
    m->jolt_ms = now;
    return true;
}

uint32_t toy_motion_still_ms(const toy_motion_t *m, uint32_t now)
{ return m->armed && m->still ? now - m->still_from : 0; }
