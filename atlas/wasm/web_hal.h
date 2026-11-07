#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void web_set_wallclock(uint32_t unix_s);
int web_take_dirty(void);
const int *web_dirty_rect(void);
const uint32_t *web_framebuffer(void);
void web_pointer(bool down, int x, int y);
void web_button(int btn, bool down);
void web_motion(int kind);
void web_link_up(int kind, bool up);
void web_deliver(int kind, const uint8_t *frame, size_t len);
