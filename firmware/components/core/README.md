# core

Portable firmware logic (C11). Depends only on `hal.h`, LVGL 9 and `agentlink_proto`; the same
sources run on the device and in the desktop simulator (`simulator/`).

Single-threaded by design: HAL callbacks (buttons, motion, link) only push into small rings
(`ring.c`); everything else runs in `core_step()`, which is also the only place that touches LVGL.

| File | Concern |
|---|---|
| `app.c` | `core_init` / `core_step`, event routing, user actions (focus, approve, deny, ack) |
| `model.c` | session store (32), attention queue, settings; sorting + counts |
| `link*.c` | handshake, rx dispatch (protocol → model), tx builders, seq numbers |
| `gesture.c` | touch/button → tap, double-tap, long-press, swipe (pure C, unit tested) |
| `feedback.c` | haptic/sound patterns per event, respecting alert mode |
| `power.c` | display idle-off, wake, alert glow |
| `debug.c` | JSON debug lines, `ui_state`, screenshot streaming, injected input |
| `ui*.c` | LVGL screens: glance, list, detail (+ attention card), theme, ring widget |

Test: `ctest --test-dir build/sim` (gesture recognizer; protocol round trip lives in `agentlink_proto`).
