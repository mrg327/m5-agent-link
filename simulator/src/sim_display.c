/* hal_display_* / hal_touch_create on SDL2. The LVGL display renders partially into small buffers;
 * flushes land in a full RGB565 framebuffer that also serves hal_display_snapshot() and PNG export.
 * The window shows that framebuffer with a round mask (outside the circle painted dark grey). */
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "sim.h"

#define W 466
#define H 466
#define BUF_ROWS 60
#define MASK_RGB 0x202020u

static uint16_t s_fb[W * H];
static uint16_t s_buf1[W * BUF_ROWS], s_buf2[W * BUF_ROWS];
static bool s_headless, s_dirty = true, s_power = true, s_quit;
static uint8_t s_brightness = 255;

static SDL_Window *s_win;
static SDL_Renderer *s_rend;
static SDL_Texture *s_tex;

static struct {
    bool down;
    int x, y;
} s_ptr;

void sim_display_set_headless(bool headless) { s_headless = headless; }
bool sim_display_quit_requested(void) { return s_quit; }
void sim_input_pointer(bool down, int x, int y)
{
    s_ptr.down = down;
    s_ptr.x = x;
    s_ptr.y = y;
}

static bool in_circle(int x, int y)
{
    int dx = x - W / 2, dy = y - H / 2;
    return dx * dx + dy * dy <= (W / 2) * (W / 2);
}

static uint32_t rgb565_to_rgb888(uint16_t c)
{
    uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

static lv_display_t *s_disp;
static uint16_t s_mark[W * H];
void sim_fb_mark(void) { memcpy(s_mark, s_fb, sizeof s_fb); }
uint32_t sim_fb_rgb(int x, int y)
{ return x < 0 || y < 0 || x >= W || y >= H ? 0 : rgb565_to_rgb888(s_fb[y * W + x]); }
int sim_fb_flipped_match_pct(void)
{
    long same = 0;
    for (int i = 0; i < W * H; i++) same += s_fb[i] == s_mark[W * H - 1 - i];
    return (int)(same * 100 / (W * H));
}

/* Like the watch's flush: a UI turned by 180 degrees is reversed pixel for pixel and its area
 * mirrored, so the framebuffer (and the PNG) hold what the glass would show. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    lv_area_t rot = *area;
    const lv_area_t *a = area;
    if (lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180) {
        uint16_t *p = (uint16_t *)px;
        for (size_t i = 0, j = (size_t)lv_area_get_size(area); i + 1 < j; i++, j--) {
            uint16_t t = p[i];
            p[i] = p[j - 1];
            p[j - 1] = t;
        }
        lv_display_rotate_area(disp, &rot);
        a = &rot;
    }
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++, src += w) memcpy(&s_fb[y * W + a->x1], src, (size_t)w * 2);
    if (lv_display_flush_is_last(disp)) s_dirty = true;
    lv_display_flush_ready(disp);
}

/* ── Per-frame redraw (the scripts' `frames` report, BENCH.md §9) ──
 * One record per LVGL refresh that flushed something, as the bench's frame record on the watch:
 * the pixels flushed, the areas LVGL rendered (after it joined the invalidated ones: LVGL flushes
 * an area top to bottom in buffer-high pieces of its full width, so a piece that does not continue
 * the one before starts a new area), the 48-row strips those areas take in the watch's partial
 * buffers, and the host CPU the refresh took (informational). Read only: it changes nothing the
 * core draws. */
#define FRAMES_MAX 8192
#define WATCH_BUF_ROWS 48 /* the StopWatch's two 466x48 buffers (FIRMWARE.md §5) */

static sim_frame_t s_frames[FRAMES_MAX];
static size_t s_frames_n;
static uint32_t s_frames_lost;
static sim_frame_t s_cur;
static lv_area_t s_cur_area; /* the area being flushed; y2 < y1 before the first */
static uint64_t s_cur_cpu_ns;

static uint64_t cpu_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void close_area(void)
{
    int32_t h = s_cur_area.y2 - s_cur_area.y1 + 1;
    if (h > 0) s_cur.strips += (uint32_t)((h + WATCH_BUF_ROWS - 1) / WATCH_BUF_ROWS);
    s_cur_area = (lv_area_t){0, 0, 0, -1};
}

static void on_frame_event(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_REFR_START:
        s_cur = (sim_frame_t){.t_ms = (uint32_t)(hal_time_us() / 1000)};
        s_cur_area = (lv_area_t){0, 0, 0, -1};
        s_cur_cpu_ns = cpu_ns();
        break;
    case LV_EVENT_FLUSH_START: {
        const lv_area_t *a = lv_event_get_param(e);
        if (!a) break;
        s_cur.px += (uint32_t)lv_area_get_size(a);
        bool more = s_cur_area.y2 >= s_cur_area.y1 && a->x1 == s_cur_area.x1 &&
                    a->x2 == s_cur_area.x2 && a->y1 == s_cur_area.y2 + 1;
        if (more) {
            s_cur_area.y2 = a->y2;
        } else {
            close_area();
            s_cur_area = *a;
            s_cur.areas++;
        }
        break;
    }
    case LV_EVENT_REFR_READY:
        if (!s_cur.px) break;
        close_area();
        s_cur.cpu_us = (uint32_t)((cpu_ns() - s_cur_cpu_ns) / 1000);
        if (s_frames_n < FRAMES_MAX) {
            s_frames[s_frames_n++] = s_cur;
        } else {
            s_frames_lost++;
        }
        break;
    default: break;
    }
}

void sim_frames_reset(void)
{
    s_frames_n = 0;
    s_frames_lost = 0;
}

const sim_frame_t *sim_frames(size_t *n, uint32_t *lost)
{
    *n = s_frames_n;
    if (lost) { *lost = s_frames_lost; }
    return s_frames;
}

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_ptr.x;
    data->point.y = s_ptr.y;
    data->state = s_ptr.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_display_t *hal_display_create(void)
{
    if (!s_headless) {
        SDL_Init(SDL_INIT_VIDEO);
        s_win = SDL_CreateWindow("m5-agent-link simulator", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, W, H, 0);
        s_rend = s_win ? SDL_CreateRenderer(s_win, -1,
                                            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)
                       : NULL;
        s_tex = s_rend ? SDL_CreateTexture(s_rend, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, W, H)
                       : NULL;
        if (!s_tex) {
            sim_log("sdl_failed", "\"error\":\"%s\"", SDL_GetError());
            s_headless = true; /* keep running without a window rather than dying */
        }
    }
    lv_display_t *d = lv_display_create(W, H);
    s_disp = d;
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_buf1, s_buf2, sizeof s_buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_display_add_event_cb(d, on_frame_event, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(d, on_frame_event, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(d, on_frame_event, LV_EVENT_REFR_READY, NULL);
    return d;
}

lv_indev_t *hal_touch_create(lv_display_t *disp)
{
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, pointer_read_cb);
    lv_indev_set_display(in, disp);
    return in;
}

void hal_display_set_power(bool on)
{
    s_power = on;
    s_dirty = true;
}
void hal_display_set_brightness(uint8_t level)
{
    s_brightness = level;
    s_dirty = true;
}
void hal_display_set_rotation_180(bool flipped)
{
    if (s_disp)
        lv_display_set_rotation(s_disp, flipped ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);
}

size_t hal_display_snapshot(uint32_t offset, uint8_t *dst, size_t max_len)
{
    size_t total = sizeof s_fb;
    if (offset >= total) return 0;
    size_t n = total - offset < max_len ? total - offset : max_len;
    /* host is little-endian, matching the wire format */
    memcpy(dst, (const uint8_t *)s_fb + offset, n);
    return n;
}

static void present(void)
{
    if (!s_tex || !s_dirty) return;
    s_dirty = false;
    uint32_t *pix;
    int pitch;
    if (SDL_LockTexture(s_tex, NULL, (void **)&pix, &pitch) != 0) return;
    for (int y = 0; y < H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pix + y * pitch);
        for (int x = 0; x < W; x++) {
            uint32_t c = MASK_RGB;
            if (in_circle(x, y)) {
                c = s_power ? rgb565_to_rgb888(s_fb[y * W + x]) : 0;
                if (s_power && s_brightness < 255) {
                    uint32_t r = (c >> 16 & 0xff) * s_brightness / 255,
                             g = (c >> 8 & 0xff) * s_brightness / 255,
                             b = (c & 0xff) * s_brightness / 255;
                    c = r << 16 | g << 8 | b;
                }
            }
            row[x] = 0xff000000u | c;
        }
    }
    SDL_UnlockTexture(s_tex);
    SDL_RenderCopy(s_rend, s_tex, NULL, NULL);
    SDL_RenderPresent(s_rend);
}

static void on_key(SDL_Keycode key, bool down)
{
    switch (key) {
    case SDLK_a: sim_input_button(0, down); break;
    case SDLK_b: sim_input_button(1, down); break;
    case SDLK_l: sim_input_motion(down ? HAL_MOTION_LIFT : HAL_MOTION_DROP); break;
    case SDLK_s:
        if (down) sim_input_motion(HAL_MOTION_SHAKE);
        break;
    case SDLK_ESCAPE: s_quit = true; break;
    default: break;
    }
}

void sim_display_pump(void)
{
    if (s_headless) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: s_quit = true; break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) sim_input_pointer(true, e.button.x, e.button.y);
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT)
                sim_input_pointer(false, e.button.x, e.button.y);
            break;
        case SDL_MOUSEMOTION:
            if (s_ptr.down) sim_input_pointer(true, e.motion.x, e.motion.y);
            break;
        case SDL_KEYDOWN:
            if (!e.key.repeat) on_key(e.key.keysym.sym, true);
            break;
        case SDL_KEYUP: on_key(e.key.keysym.sym, false); break;
        default: break;
        }
    }
    present();
}

bool sim_save_png(const char *path)
{
    uint8_t *rgb = malloc((size_t)W * H * 3);
    if (!rgb) return false;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t c = in_circle(x, y) ? rgb565_to_rgb888(s_fb[y * W + x]) : MASK_RGB;
            uint8_t *o = rgb + ((size_t)y * W + x) * 3;
            o[0] = (uint8_t)(c >> 16);
            o[1] = (uint8_t)(c >> 8);
            o[2] = (uint8_t)c;
        }
    }
    bool ok = sim_png_write(path, rgb, W, H);
    free(rgb);
    return ok;
}
