# UI & Animation Best Practices – M5Stack StopWatch (ESP32-S3R8, CO5300 466x466 round AMOLED)

Legend: **[V]** = verified by reading source/docs in this research session (URL cited);
**[K]** = established knowledge / general practice, *not* re-verified here — check before relying on it;
**[?]** = open question to measure on hardware.

---------------------------------------------------------------------------------------------------

## 0. Executive summary

* The factory firmware (`m5stack/M5StopWatch-UserDemo`) is **ESP-IDF 5.5.4 + LVGL v9.5.0 + M5GFX 0.2.19**,
  with Forairaaaaa's `mooncake` (app framework) and `smooth_ui_toolkit` (spring/easing animation lib). **[V]**
  Sources: `repos.json`, `README.md`, `sdkconfig.defaults`.
* It drives the AMOLED through **M5GFX's `Panel_AMOLED` (QSPI @ 80 MHz, SPI2, DMA auto)**, not `esp_lcd`. LVGL
  is bolted on via a hand-written flush callback: `LV_DISPLAY_RENDER_MODE_PARTIAL`, **two 466x120 RGB565
  buffers in PSRAM**, LVGL task on `lv_timer_handler()` + 2 ms sleep. **[V]**
* The factory port is **not** optimal: PSRAM partial buffers, flush is a CPU-driven `writePixels` loop in 8192-px
  chunks (with a comment about avoiding an M5GFX SIMD bug), the TE pin is configured as an input but I found
  **no TE-synchronised flush** in `hal_display.cpp`, and there's no 2-pixel rounder. **[V]** (absence = within
  that file only; other files not exhaustively grepped).
* Recommendation: **ESP-IDF (PlatformIO-espidf or plain idf.py) + LVGL 9.x + M5GFX as the panel/bus driver**
  (reuse the vendor's proven `Panel_CO5300` init & HAL code), partial render mode with **internal-SRAM
  double buffers (~466x40..48 px)**, own flush using DMA, optional TE gating, small-region animations,
  black backgrounds, 30 fps UI target (60 only for tiny dirty regions).

---------------------------------------------------------------------------------------------------

## 1. What the factory firmware does (verified)

### 1.1 Repo layout / stack
* Repo: https://github.com/m5stack/M5StopWatch-UserDemo (main). Layout: `main/{apps,assets,hal}`, `patches/`,
  `platformio.ini`, `sdkconfig.defaults`, `sdkconfig.m5stack-stopwatch`.
* `repos.json` pinned deps **[V]**: `mooncake v2.3.3`, `mooncake_log v1.5.0`, `smooth_ui_toolkit v2.12.1`,
  `M5GFX 0.2.19`, `ArduinoJson v7.4.3`, **`lvgl v9.5.0`**, `M5IOE1 1.0.8`, `M5PM1 1.0.6`,
  `BMI270_BMM150_Sensor 0.1.2`.
  https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/repos.json
* Toolchain: ESP-IDF v5.5.4 (README). PlatformIO variant (pioarduino 55.03.312, `framework = espidf`, board
  `esp32s3box`, QIO flash @80 MHz) is included — i.e. **PlatformIO works for this device in ESP-IDF mode**.
  https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/platformio.ini
* Apps in `main/apps`: `app_launcher, app_watch_face, app_stopwatch, app_alarm_clock, app_fluidbox, app_fft,
  app_imu, app_lucky_wheel, app_setup, app_badge, app_template, common`.
  The `sdkconfig.defaults` also enables `LV_USE_DEMO_SMARTWATCH/BENCHMARK/STRESS`.

### 1.2 sdkconfig.defaults (verbatim highlights) **[V]**
https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/sdkconfig.defaults
```
CONFIG_SPIRAM=y  CONFIG_SPIRAM_MODE_OCT=y  CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_LV_USE_CLIB_MALLOC=y            # LVGL uses libc malloc (-> PSRAM-capable heap)
CONFIG_LV_CACHE_DEF_SIZE=1048576       # 1 MB image cache
CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=2
CONFIG_LV_FONT_MONTSERRAT_{10,16,18,22,28,36}=y
CONFIG_LV_USE_FS_STDIO=y  CONFIG_LV_USE_LODEPNG=y  CONFIG_LV_USE_TJPGD=y
```
Not set: `LV_ATTRIBUTE_FAST_MEM_USE_IRAM`, `COMPILER_OPTIMIZATION_PERF`, SPIRAM_FETCH_INSTRUCTIONS, 2 draw units.

### 1.3 Display bring-up (hal_display.cpp) **[V]**
https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/hal/hal_display.cpp
* Pins: SCLK=G40, IO0=G41, IO1=G42, IO2=G46, IO3=G45, CS=G39, **TE=G38**, RST=NC (reset via M5IOE1 PYG5,
  panel power via PYG8 — see `docs/StopWatch.md`).
* Local `Panel_CO5300 : lgfx::Panel_AMOLED`, `memory 480x480`, rgb565 write depth; config `panel_width=468,
  panel_height=466, offset_x=6`. Bus: `Bus_SPI`, `freq_write=80 MHz`, SPI2_HOST, mode 0, 3-wire, DMA auto.
* Init command list: `0x11` sleep-out (150 ms), `0xC4 80`, `0x35 80` (TE on), `0x44 01 D2` (**TE scanline = 0x1D2
  = 466**), `0x53 20` (brightness control on), `0x36 00` (MADCTL), `0x51 A0` (initial brightness), `0x29` display on.
* `lgfx::pinMode(TE, input_pullup)`; then `enableFrameBuffer(true)` → `initPanelFb()` creates a **full
  frame-buffer panel** (`Panel_AMOLED_Framebuffer`, with `setAutoDisplay`), so M5GFX draws into RAM and
  `display()` pushes regions. Brightness via `_panel.setBrightness(0..255)` → command `0x51`.
  Settings map 0–100 % → 0–255; the factory clamps minimum user brightness at 10 %.
* LVGL port (copied from `m5stack/lv_m5_emulator/.../lvgl_port_m5stack.cpp`):
  * `lv_tick_inc(10)` from 10 ms `esp_timer`; task `lvgl_rtos_task` (16 KB stack, prio 1) calls
    `lv_timer_handler()` every ~2 ms under a mutex.
  * `lv_display_set_buffers(disp, buf1, buf2, 466*120*?, PARTIAL)` with both buffers
    `heap_caps_malloc(width*LV_BUFFER_LINE, MALLOC_CAP_SPIRAM)`, `LV_BUFFER_LINE = 120`
    (note: bytes = 466*120 = 55 920 B, which is *half* of 466x120 RGB565 = 111 840 B; the size passed to
    `set_buffers` is also `width*120` bytes — verify; looks like an effective 60-row buffer **[?]**).
  * Flush = `setAddrWindow` + `writePixels` in 8192-px chunks, comment: "avoid M5GFX SIMD optimizations …
    problematic copy_rgb_fast". No TE wait, no DMA-completion async (flush_ready is called synchronously).
  * Touch: `Cst820` custom driver → LVGL pointer indev; no gesture events from the chip are used.

### 1.4 M5GFX CO5300 support
* `M5GFX/src/lgfx/v1/panel/Panel_AMOLED.{hpp,inl}` (QSPI, `LGFX_USE_QSPI`): `setBrightness` (0x51), `setSleep`
  (0x10/0x11), `setPowerSave`, `setPartialArea` (0x30/0x31), `waitDisplay/displayBusy`, `write_bytes(..use_dma)`,
  framebuffer sub-panel. https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/panel/Panel_AMOLED.hpp
* M5GFX's own `Panel_CO5300.hpp` is for LilyGO T-Watch-Ultra (502x410) — so the StopWatch overrides it locally.
  https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/panel/Panel_CO5300.hpp
* Arduino path (vendor docs in `docs/arduino/api/display.md`, `touch.md`): `M5.begin()` then `M5.Display`
  (M5GFX); needs Board Manager ≥3.3.7, M5Unified ≥0.2.15, M5GFX ≥0.2.21.
  Touch via `M5.Touch.getDetail()` with states none/touch/touch_end/touch_begin/hold/hold_end/hold_begin/
  flick/flick_end/flick_begin/drag/drag_end/drag_begin (flick/drag are M5Unified software gesture states).

---------------------------------------------------------------------------------------------------

## 2. Choosing a graphics stack

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| **LVGL v9 + M5GFX panel driver (factory approach)** | Proven on this exact HW; widgets, styles, anims, scroll-snap, arcs, transforms, gestures; huge ecosystem; factory has an LVGL smartwatch demo enabled | LVGL CPU rendering at 466² is heavy; M5GFX flush glue must be tuned | **Recommended** |
| LVGL v9 + `esp_lvgl_port` + `espressif/esp_lcd_co5300` | Official Espresso path; DMA-queued async flush, `avoid_tearing`, rounder cb, rotation, task/timer handled; component exists (v2.2.0 on registry **[V]**) | Needs StopWatch-specific init (reset/power via M5IOE1 I²C expander before `esp_lcd` init); different touch driver glue; CO5300 module-specific init sequence may need override **[?]** | Good alternative if more throughput needed |
| M5GFX/LovyanGFX sprites only (immediate mode) | Very small, fast primitives, RGB565 sprites in PSRAM, `pushSprite`, `pushRotateZoom`, anti-aliased `fillArc`, smooth fonts; excellent for 1–2 custom screens (clock face, gauges) | No widgets/layout/gesture framework; full-frame 466²×2 = 434 KB sprite lives in PSRAM; do-it-yourself dirty-rect | Best for single-purpose "watch-face" / game-like canvases; can be embedded in LVGL as a canvas |
| TFT_eSPI / Arduino_GFX | Simple | No (or poor) CO5300 QSPI support for this board | Skip |
| `smooth_ui_toolkit` (spring/easing) | Already used by factory; works with LVGL (`uitk::lvgl_cpp`) | Third-party, sparse docs | Optional; LVGL's own `lv_anim` is enough |
| Others (SquareLine Studio, LVGL Lottie/ThorVG) | Design tooling | Heavy | See §4.4 |

### 2.1 Framework: Arduino vs ESP-IDF vs PlatformIO
* **ESP-IDF (idf.py)** – what M5 ships; full control of sdkconfig (PSRAM XIP, cache sizes, IRAM placement,
  `LV_*` via Kconfig), `esp_lcd` drivers, proper FreeRTOS core pinning. **Best for perf-sensitive UI.**
* **Arduino (M5Unified / M5GFX)** – fastest to start (vendor docs target it) but sdkconfig is fixed by the
  precompiled Arduino core; cannot tune PSRAM XIP / cache line / `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` easily.
  Arduino-as-ESP-IDF-component is supported by M5GFX (README note, `docs/github/M5GFX.md`) **[V]**.
* **PlatformIO** – works either way: factory repo ships a `platformio.ini` with `framework = espidf` on the
  pioarduino platform **[V]**. Use `framework = espidf` (or `arduino, espidf`) if you want PIO's tooling with IDF's sdkconfig.
* This project already has agent-link / Wi-Fi code in `docs/` (Hermes gadget SDK); Arduino-as-component keeps
  that library access if needed.

---------------------------------------------------------------------------------------------------

## 3. Rendering performance

### 3.1 Numbers & arithmetic (derived)
* Frame = 466×466×2 B = **434 312 B (424 KiB)** RGB565. Round display actually shows ~π/4 ≈ 78.5 % of those pixels
  (≈ 170 k of 217 k) — the panel still receives rectangles, so corners are transmitted/rendered unless clipped.
* QSPI @ 80 MHz × 4 bits = 40 MB/s theoretical → **~10.9 ms/full frame** at line rate; practically ~60–75 %
  after command overhead/DMA gaps ⇒ **~14–18 ms ⇒ max ≈ 55–70 fps of raw transfer** **[K, derive/measure]**.
  CO5300 datasheet (in `docs/datasheets/CO5300_display_driver.pdf`) lists the allowed QSPI clock; M5 uses 80 MHz **[V]**.
* PSRAM octal 80 MHz: theoretical ~160 MB/s DDR peak (8 bit × 80 MHz × 2); practical ESP32-S3 sustained
  **~30–60 MB/s read/~30–50 MB/s write via cache (and shared with DMA)** **[K]**. Espressif guidance: use
  `CONFIG_SPIRAM_SPEED_80M`, data cache 64 KB, cache line 64 B to improve PSRAM throughput **[V — set in the
  Waveshare 1.75 LVGL example sdkconfig.defaults, see §8]**.
* LVGL SW render of full-screen 466² alpha/gradient/transform content on ESP32-S3: typically **8–25 fps**; simple
  fills/arcs/text small regions: well above 30 fps **[K/?]** — measure with `LV_USE_PERF_MONITOR`.
  LVGL's own benchmark demo is enabled in the factory sdkconfig (`LV_USE_DEMO_BENCHMARK`) – run it first.

### 3.2 Buffers: PSRAM full-frame vs SRAM partial
| Strategy | RAM | Pros | Cons |
|---|---|---|---|
| Partial, 2× in **internal SRAM** (e.g. 466×40 ×2 B ×2 = 74 KB) | ~75–100 KB DMA-capable internal | Fastest rendering (internal SRAM ≫ PSRAM), DMA reads straight from SRAM, render-while-flushing overlap | More chunks per frame; every 2-px-alignment rule applies; limited internal RAM (WiFi/BLE also need it) |
| Partial, 2× in PSRAM (factory: 466×120 class) | ~110 KB PSRAM | Zero internal RAM | PSRAM is slow for render read-modify-write and contends with cache; DMA from PSRAM needs EDMA, fine on S3 but slower |
| Full-frame ×1 in PSRAM, `RENDER_MODE_DIRECT` | 424 KB | Only dirty areas re-rendered, simplest semantic for anims | Rendering to PSRAM; flush must copy dirty rects to panel; tearing if flush overlaps render of next frame |
| Full-frame ×2 in PSRAM (`FULL`/`DIRECT`) | 848 KB | Classic tear-free ping-pong; can swap pointers w/ TE | Eats PSRAM bandwidth; full flush each time in FULL mode (~15 ms) |

Rules of thumb **[K]** (consistent with LVGL docs/forum guidance, https://docs.lvgl.io/master/):
1. Prefer **PARTIAL + 2 internal-SRAM DMA buffers, ≥ 1/10 screen** (LVGL's recommended minimum), here 466×48 ≈ 1/10.
2. Allocate with `heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)` (esp_lvgl_port: `buff_dma=true`).
3. Use **`LV_DRAW_SW_DRAW_UNIT_CNT=2`** to use both cores for render **[V in Waveshare cfg]**; but keep touch/IMU/Wi-Fi
   on the other core carefully (LVGL task `task_affinity`).
4. Put UI assets that are hit per pixel (fonts, gradients LUT) in internal RAM/flash cache; big images in
   PSRAM or flash-mmap (see below).
5. Full-frame PSRAM only if you want effects touching the whole screen every frame (zoom/rotate/blur).

### 3.3 DMA & flush design
* esp_lvgl_port exposes `trans_size`, `buff_dma`, `buff_spiram`, `double_buffer`, `flags.swap_bytes` **[V]**
  (`esp-bsp/components/esp_lvgl_port`; config struct shown in README, https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port).
* Byte order: RGB565 needs byte-swap for 8-bit-wide SPI. Either set `LV_COLOR_16_SWAP`-equivalent
  (LVGL 9: `lv_draw_sw_rgb565_swap()` in flush, or `flags.swap_bytes`) or let M5GFX's `writePixels`
  take `rgb565_t`/`swap565_t` (factory uses `lgfx::rgb565_t*`). **[V]** the factory cast. The swap is a measurable
  cost; ESP32-S3 SIMD (below) can accelerate it.
* Async pattern: flush_cb starts DMA and returns; call `lv_display_flush_ready()` from the transfer-done ISR/callback
  (esp_lcd `on_color_trans_done`). The M5GFX route: `startWrite(); setAddrWindow; writePixels(..); endWrite()`
  is **synchronous** per factory — to overlap, use `pushPixelsDMA` + `waitDMA()`/`displayBusy()`, and call
  `lv_display_flush_ready` after `waitDMA` in the LVGL task's next tick **[K]**.
* Do **not** let any other task touch SPI2 / use the bus concurrently with the flush (M5GFX uses a bus lock).

### 3.4 TE (tearing effect) sync
* TE is enabled in the init list (`0x35`), TE scanline set by `0x44 01 D2` (466), pin G38 **[V]**. TE pulses once
  per panel refresh (typ. 60 Hz AMOLED; AMOLED internal refresh may be 60 Hz in normal mode, lower in AOD/idle) **[K]**.
* Methods: (a) ISR on G38 gives a binary semaphore; flush_cb of the **first** chunk of a frame waits for it
  (esp_lvgl_port has `.flags.avoid_tearing` for RGB/DSI only — for QSPI you implement it yourself **[K]**).
  (b) Start sending shortly *after* TE; because QSPI writes (~15 ms for full frame) are about one refresh period (16.7 ms),
  a full-frame write chasing the beam works only if the transfer is faster than the scan — CO5300 GRAM is written
  faster than it is read out, so start right after TE **[K]**. (c) For **partial** updates TE sync matters mainly for
  big, fast-moving regions; small updates rarely tear visibly.
* Practical: gate only when `lv_display_flush_is_last()` / area covers > ~25 % of screen; otherwise skip the wait to save
  latency. Measure with a scrolling vertical bar test. **[?]**

### 3.5 Round-display / CO5300 alignment ("rounder")
* QSPI AMOLED controllers (CO5300, SH8601, RM67162) require **start/end X and Y to be even** (x1 even, x2 odd; same Y) for
  `0x2A/0x2B` windows — otherwise pixel shifts/garbage **[K, widely reported; esp_lcd_co5300 docs and Waveshare/LilyGO
  examples use the rounder; verify against CO5300 datasheet in `docs/datasheets/` [?]]**. LVGL 9 callback:
```c
static void rounder_cb(lv_event_t *e) {          // LV_EVENT_INVALIDATE_AREA
    lv_area_t *a = lv_event_get_invalidate_area(e);
    a->x1 &= ~1;  a->y1 &= ~1;                    // start even
    a->x2 |= 1;   a->y2 |= 1;                     // end odd
}
lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
```
  (LVGL 8 used `disp_drv.rounder_cb`; esp_lvgl_port exposes `.rounder_cb` in `lvgl_port_display_cfg_t` **[V]**.)
  Note: the factory firmware **does not** register a rounder (not in `hal_display.cpp`) but M5GFX handles windowing inside
  `setWindow`/`writeImage`; if you swap to your own flush, add it.
* Panel offset: factory uses `panel_width=468`, `offset_x=6`(!). M5's 466 visible area is centered in 480 GRAM → odd
  offsets can break the alignment rule; keep it as M5 configured. **[V]** values; **[?]** why 468.
* Round-aware optimisation: clip invalid areas to the circle bounding rows (skip rows' dead corners) — only
  worthwhile for large full-width redraws (use per-row start/length in your own flush) **[K]**.

### 3.6 Rotation
* Don't use LVGL software rotation (`lv_display_set_rotation`) at 466² partial mode: it costs CPU + needs extra buffer.
  Use panel MADCTL `0x36` (M5GFX `setRotation`) for 0/90/180/270; round screen → usually **no rotation needed** **[K]**.
* If you rotate widgets (arc needles, watch hands) use LVGL `lv_image` with `rotation` + `pivot`, only the small hand
  image area is transformed, bounded by the arm's bbox (see §4).

### 3.7 CPU tricks: IRAM, SIMD/PIE, esp-dsp
* `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y` (put LVGL hot functions in IRAM) **[V in Waveshare cfg]**,
  `CONFIG_COMPILER_OPTIMIZATION_PERF=y`, `CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB`, `CONFIG_ESP32S3_DATA_CACHE_64KB`,
  `CONFIG_ESP32S3_DATA_CACHE_LINE_64B`, `CONFIG_SPIRAM_FETCH_INSTRUCTIONS/RODATA` **[V]** (all in
  https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/examples/esp-idf/02_lvgl_demo_v9/sdkconfig.defaults).
  Caveat: FETCH_INSTRUCTIONS/XIP-from-PSRAM frees internal RAM but eats PSRAM bandwidth; use if IRAM is short. **[K]**
* LVGL ≥ 9.2 has **ESP32-S3 PIE (SIMD) assembly blend routines** (`LV_USE_DRAW_SW_ASM = LV_DRAW_SW_ASM_CUSTOM`,
  `src/draw/sw/blend/neon`/`helium` for ARM; for Xtensa ESP32-S3 via Espressif's esp_lvgl_port "`lv_draw_sw_blend_esp32s3`"-style
  asm accel merged in LVGL 9.3 **[K; verify name]**). Free ~10–30 % on RGB565 fill/blend **[?]**.
* M5GFX contains its own `copy_rgb_fast`/SIMD paths — the factory author disabled them via chunking in flush because of
  a bug **[V]**, so test before relying on M5GFX SIMD with PSRAM sources.
* esp-dsp (https://github.com/espressif/esp-dsp) helps FFT/filters (the factory has `app_fft`), not UI blend. Use it for
  audio visualisers, easing LUT generation via `dsps_*` is overkill.
* Misc: `CONFIG_LV_OBJ_STYLE_CACHE=y`, `CONFIG_LV_DEF_REFR_PERIOD=15` **[V]**; `LV_USE_FLOAT` off; use `lv_color_format_t RGB565`
  (not ARGB8888) at the display; avoid per-pixel alpha layers (`LV_COLOR_FORMAT_ARGB8888` layers + opa<255 groups
  allocate layer buffers in the heap).

---------------------------------------------------------------------------------------------------

## 4. Animation best practices

### 4.1 Frame-rate targets **[K]**
* **30 fps** for UI transitions on LVGL CPU rendering; **60 fps** only when dirty area ≤ ~1/6 screen or content is
  simple (fills, arcs, bitmaps). `LV_DEF_REFR_PERIOD` 33 ms (30 fps) default; 16 ms for 60.
* Consistent pacing beats peak fps: keep a steady 30 than oscillate 20–45. Budget: **33 ms/frame** = ~15 ms render + ~15 ms
  flush (overlap them with double buffers → ~max(render, flush)).
* Idle = **no animation, no timers** → `lv_timer_handler()` returns large delay → `vTaskDelay(that)` (esp_lvgl_port's
  `task_max_sleep_ms`), CPU can light-sleep.

### 4.2 Easing
* Ease-out (cubic/quint) for entering elements, ease-in for exits, ease-in-out for position changes; durations
  **150–250 ms** for micro-interactions, **250–400 ms** for screen transitions (Wear OS Material guidance
  ~300 ms; Apple HIG similar) **[K]**. Avoid linear except for spinners/progress.
* LVGL: `lv_anim_set_path_cb(&a, lv_anim_path_ease_out)`, `lv_anim_path_overshoot`, `lv_anim_path_bounce`; custom cubic bezier via
  `lv_anim_path_custom_bezier3` (`lv_anim_set_bezier3_param`). Spring physics: factory's `smooth_ui_toolkit` provides `Spring`/`Easing`
  (v2.12.1 **[V]** in repos.json) — nice "iOS-like" feel for drag-release.
* Drive animations by **time not frames** (LVGL does); never block the LVGL task.

### 4.3 Minimise dirty area
* Every changed object invalidates its bbox → whole union is rendered. Strategies **[K]**:
  - Animate **small**, opaque-background widgets (arc indicator, hand image, icon) – keep bbox tight.
  - Use `lv_obj_set_style_transform_*` / `translate` on small objects rather than relayout (avoids invalidating siblings).
  - Avoid `opa<255`, shadows, blur, big radius clipping masks (`lv_obj_set_style_clip_corner`, `radius=LV_RADIUS_CIRCLE` + clip) on large objects — they trigger
    layer/mask rendering (slow). Pre-render those as images.
  - Avoid `lv_label` text changes in animated parents that cause re-layout.
  - Use `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` + `lv_obj_remove_flag(LV_OBJ_FLAG_SCROLLABLE)` where applicable.
  - Use `LV_OBJ_FLAG_SNAPPABLE` + `lv_obj_set_scroll_snap_*` for pages; `scroll_throw` tuned.
* Debug with `LV_USE_REFR_DEBUG` (flash dirty areas) and `LV_USE_PERF_MONITOR` **[V enabled in Waveshare demo]**.
* For purely custom canvases (watch hands, particles — as FluidBox/lucky-wheel in factory), M5GFX sprites + `pushSprite` of
  a bounding rect is ideal and bypasses LVGL cost; use a 16-bit sprite in PSRAM with `setPsram(true)` (factory has commented code for a full-screen canvas **[V]**).

### 4.4 Pre-rendered assets vs vector/Lottie
* **Pre-rendered sprites/PNG-sequence/RGB565 raw (`LV_IMAGE_COMPRESS`, `.bin` in flash/LittleFS, mmap)**: cheapest; 466² frame
  = 424 KB, so keep animated assets ≤ ~120×120 (28 KB/frame, 20–30 frames) or use RLE/LZ4-compressed LVGL images
  (`LV_USE_LZ4`, `LV_BIN_COMPRESS`) **[K]**.
* **Lottie** (`LV_USE_LOTTIE` in LVGL 9.4+ via ThorVG; rlottie in older/LVGL8 ports): vector → CPU raster to an ARGB8888 canvas
  (4 B/px): a 150×150 canvas = 90 KB; ThorVG on ESP32-S3 typically a few–15 fps for small sizes, and adds ~200–400 KB flash and
  significant PSRAM **[K/?]**. Good for one-off hero animations (success checkmark, loading), not continuous full-screen. Measure; prefer
  pre-rendered frames for anything frequent.
* **Arc/spinner**: `lv_arc` (rotating indicator via `lv_arc_set_rotation` or `lv_anim` on value) and `lv_spinner` are cheap if the arc
  thickness ≥ ~8 px and background is opaque; anti-aliased arcs cost more per edge px but are bounded by the ring bbox. On a 466 round screen the
  ring bbox is the full screen → invalidation of whole square! **Mitigation:** animate a short arc segment, or use 4 quadrant-sized
  invalidations by splitting into several `lv_arc`s/images; or draw spinner as a pre-rendered rotating sprite (rotate ≤ 120×120 image). **[K]**
* Compared: factory `app_fluidbox`/`app_lucky_wheel` use custom M5GFX drawing for complex animations **[V dir listing]**.

### 4.5 Round-screen transition patterns (Wear OS / watchOS / Pixel Watch) **[K]**
* **Edge/bezel swipe**: swipe from screen edge = back (Wear OS swipe-to-dismiss from left); watchOS: swipe up/down = Control Center / notifications. For
  a custom device with a "circle" use **swipe from the bezel inward** reserved for system, interior for content.
* **Page carousel with vertical scroll + horizontal swipe** (watch faces left/right; apps list up; notifications down): LVGL `lv_tileview` with
  `LV_DIR_*` per tile; use snap + overshoot ease.
* **Radial/circular menus**: items placed on a circle (r ≈ 150–170 px from center for 466 px), selected item scales ×1.2 and
  center shows label; rotate via touch drag angle (atan2). Animate using `lv_anim` on angle + per-item transform (cheap — tiny icons).
* **Scaling lists (Wear OS `ScalingLazyColumn`)**: items near vertical center are full size/opacity, items near top/bottom shrink (to ~70 %)
  and fade; implement in LVGL with `LV_EVENT_SCROLL` handler computing `y` distance to center → `lv_obj_set_style_transform_scale` (LVGL's
  "scroll with curve" example `lv_example_scroll_6`/"Circular list" in LVGL docs: horizontal shift via circle equation
  `x = r - sqrt(r² - dy²)`) https://docs.lvgl.io/master/widgets/ (see Scroll examples) **[K, verify URL path]**.
* **Curved text**: LVGL has no built-in; options: place per-character labels on arc (for ≤12 chars), or pre-render text to image and
  rotate; or `LV_USE_VECTOR_GRAPHIC` + ThorVG text path (heavy). Keep curved text for the top/bottom 12 o'clock/6 o'clock arcs only. **[K]**
* **Transitions**: slide with parallax (old screen moves 30 %, new 100 %), fade-through, zoom-from-icon (scale 0.6→1 + fade) — all via
  `lv_screen_load_anim()` (`LV_SCREEN_LOAD_ANIM_MOVE_LEFT/FADE_IN/OVER_*`) 200–300 ms. Prefer `MOVE_*` (copies strips; cheap) over
  fade (alpha blends full screen). Use `auto_del=false` for cached screens.

### 4.6 Haptics paired with animations **[K]**
* The StopWatch has a vibration motor (`docs/arduino/api/vibration.md`, driven via M5PM1/M5IOE1 — see local docs for the API). Keep haptics
  **short (10–30 ms) and sparse**: tick on detent/snap in scroll lists, soft pulse on confirm, double pulse on error/alarm; never haptic every frame.
* Fire haptic at the animation's **moment of impact** (end of ease-out / snap point), not at start; debounce ≥ 80 ms between ticks. ERM motors have
  ~30–50 ms spin-up so trigger ~30 ms *early*. Run haptics from a separate short-lived task/timer (I²C expander writes are slow and must not block LVGL).
* Pair "success": check animation 300 ms + single 20 ms pulse; "warning": 2×20 ms.

---------------------------------------------------------------------------------------------------

## 5. AMOLED-specific practice

* **True black (#000000) = pixels off** → near-zero power for those pixels. Use black backgrounds everywhere; dim greys instead of
  white fills; avoid large bright/white areas (white full-screen can draw several× more than black UI) **[K]**. The factory sets
  `screen.setBgColor(lv_color_black())` **[V]**. Use dark themes (`lv_theme_default_init(..., dark=true)`), saturated mid-tone accent colours
  (blue is the most power-hungry subpixel and ages fastest).
* **Brightness control**: CO5300 command `0x51` (WRDISBV, 0–255) — M5GFX `setBrightness` **[V]**; `0x53 0x20` enables brightness control
  block (in init **[V]**). Use gamma-aware mapping (e.g. `b = 255*(p/100)^2`) since AMOLED brightness looks perceptually linear only after gamma;
  factory maps linear 0–100 → 0–255 and clamps min 10 % **[V]**. Auto-dim: 100 % → 30–40 % after 10 s idle → off at 30 s **[K]**.
  Command `0x51 0x01` is used in M5GFX's CO5300 init as "dark" **[V]** (LilyGO list); high-brightness (HBM) via `0x63` **[V present in that list]**.
* **Always-on display (AOD)**: CO5300 supports idle/low-power modes (`0x39` idle mode on / `0x38` off, partial mode `0x12/0x13`, partial area
  `0x30/0x31` — M5GFX exposes `setPartialArea` & `setVerticalPartialArea` **[V]**, "not sure what they're for, more testing needed" per source comment).
  Practical AOD recipe **[K/?]**: render a minimal black screen (thin outline hands/digits, ≤ ~5–10 % lit pixels), brightness ~10–20 %
  (`0x51`), and either keep the MCU in light-sleep with the panel holding GRAM (no refresh needed, CO5300 self-refreshes from GRAM), or enable idle mode
  to reduce colour depth/refresh. Update once per minute only. The GRAM retention across ESP32 sleep is the key benefit of a GRAM-based QSPI panel.
* **Burn-in avoidance**: in AOD shift whole image by ±1–3 px each minute (pixel-shift pattern; keep content ≥ 6 px from the rim; round panel 466² has
  `offset_x=6` in the factory cfg **[V]** meaning some GRAM margin exists), vary AOD layout (outline-only digits), invert colour polarity periodically, avoid
  static high-brightness elements (status bar/battery icon) in active UI for long idle, auto-dim + timeout, cap peak brightness in static screens
  (< 50 %).
* **Display sleep/wake**: `setSleep(true)` → `0x10` Sleep In, `setSleep(false)` → `0x11` Sleep Out (needs ≥ 120 ms before next commands; the factory waits
  150 ms **[V]**), or `0x28/0x29` display off/on (faster, GRAM kept). Power rail PYG8 (`PYB_L3B_EN`, AMOLED supply) via M5IOE1 can be cut for deep power
  saving — then a full re-init is needed (`docs/StopWatch.md` lines 279–282 **[V]**).
* **Wake sources**: touch (CST820B IRQ), buttons, BMI270 wrist-tilt/any-motion interrupt (PMIC `PMG0` = RTC_INT & IMU_INT wake signal **[V]**
  `docs/StopWatch.md`:228). Use BMI270's built-in feature engine (wrist-wear-wake-up / any-motion) so the ESP32 can light-sleep; on wake, turn the panel
  on at low brightness **before** rendering the first frame to hide latency, ramp brightness up over ~150 ms. Use the PowerDemo repo (`docs/github/M5StopWatch-PowerDemo.md`)
  for the factory's low-power implementation.
* Battery: 450 mAh — at ~25 % screen lit, 50 % brightness the panel itself is the main draw; Wi-Fi modem sleep + display-off default is the biggest win.
  Target "screen on ≤ 5 s per glance".

---------------------------------------------------------------------------------------------------

## 6. Clean, glanceable UI on a 1.75" round panel

* Pixel density: 466 px / 1.75 in diagonal-diameter ⇒ **~266 ppi** (diameter 1.75 in ⇒ 466/1.75). A 1 mm ≈ 10.5 px. **[derived]**
* **Touch targets**: ≥ **7–9 mm** (Material/Wear OS min 48 dp ≈ 7–9 mm) ⇒ **≥ 80 px** (prefer 90–110 px); spacing ≥ 8 px; place primary
  buttons in the lower half center; avoid targets within ~30 px of the rim (touch edge accuracy and bezel). Full-width bottom "pill" button ~ 200×80 centered. **[K]**
* **Safe area**: the visible circle: usable inscribed square ≈ 466·0.707 = **330×330**; keep critical text within r ≤ 200 px from center;
  top/bottom 12 o'clock/6 o'clock items only up to ~(width ≈ 2·√(r²−dy²)).
* **Typography** **[K]** (viewed at ~30–40 cm, 266 ppi): body ≥ **22–24 px (≈ 6 pt-ish)**, secondary ≥ 18 px, hero numerals 64–120 px, **never < 16 px**.
  Use medium/semibold weights on black (thin weights bloom and alias on AMOLED PenTile-style subpixels). Factory LVGL fonts: Montserrat 10/16/18/22/28/36 **[V]**;
  Waveshare demo uses 12–26 **[V]**.
  - **LVGL fonts**: built-in Montserrat (4 bpp antialiased) — enable only sizes you use (each ~20–60 KB flash); custom via
    **lv_font_conv** (https://github.com/lvgl/lv_font_conv, `--bpp 4 --format lvgl --no-compress`, `--lcd`not for AMOLED) or the online tool
    https://lvgl.io/tools/fontconverter; limit glyph ranges (`0x20-0x7F`, plus icons); compressed fonts (`LV_USE_FONT_COMPRESSED` **[V enabled in Waveshare]**) save flash at CPU cost.
  - **Runtime TTF**: LVGL `LV_USE_TINY_TTF` (stb_truetype; cache glyphs, RAM-heavy; first-draw latency — OK for static text, avoid in animated counters)
    or FreeType (heavier). For big numerals pre-render to bitmap font (e.g. 96 px digits only `0123456789:.`).
  - **M5GFX/LovyanGFX**: `fonts::FreeSansBold18pt7b`, smooth fonts via VLW (`loadFont`) – antialiased. (Factory touch demo uses FreeMonoBold18pt7b **[V]**.)
  - Prefer fixed-width (tabular) digits for timers to avoid jitter (Montserrat digits are tabular; check your custom font).
* **Colour semantics** **[K]**: on black — white/near-white (#F2F2F2) primary text, 60 % grey (#9AA0A6) secondary; accents:
  green #34C759 OK/active/running, amber #FFB020 warning/waiting, red #FF453A error/stop, blue #0A84FF/cyan info/link, purple for agent "thinking".
  Don't convey state by colour alone (add icon/shape, text); contrast ≥ 4.5:1. Limit palette to 1 accent per screen + state colours.
  For an agent-status device: ring colour around the screen edge = state (idle dim blue, working pulsing amber, needs-input green/magenta, error red) —
  a **thin 6–8 px ring** at the rim is visible from arm's length and cheap if animated via a pre-rendered ring/`lv_arc` limited to opacity change. **[idea]**
* **Icons**: LVGL built-in `LV_SYMBOL_*` (FontAwesome subset in Montserrat), Material Symbols (generate a font with lv_font_conv from the TTF using
  selected codepoints — recolourable, tiny), or pre-rendered A8 (alpha-only 8-bit) PNG/bin icons recoloured by `lv_obj_set_style_image_recolor` —
  best size/quality (e.g. 48×48 A8 = 2.3 KB). Use 48–64 px glanceable icons, 96 px for hero.
* **Lists on round screens**: ≤ 3–5 visible rows; center row emphasised; row height ≥ 80 px; labels width limited to chord width; use the scaling list
  pattern (§4.5) with item width shrinking toward top/bottom. Paginate instead of long lists; alternative: **one item per page** (swipe up/down), very glanceable.
* **Glanceability**: one primary metric ≥ 64 px, ≤ 3 lines of text, ≤ 2 s to comprehend; progressive disclosure on tap; haptic + subtle motion for state changes.
  Use a consistent "centre = what, rim = status" layout.
* Page indicator: small dots/arc at rim (4–6 px) — in AOD hide.

---------------------------------------------------------------------------------------------------

## 7. Touch & gestures (CST820B)

* CST820B (`docs/datasheets/CST820B_touch.pdf`): single-point capacitive controller; reports finger count + X/Y over I²C at 0x15 and has a hardware
  **gesture ID register** (swipe up/down/left/right, single click, double click, long press; reg 0x01 in the common CST816/820 family) **[K — verify in the datasheet]**.
  Reset line via M5IOE1 PYG4 **[V]** (`docs/StopWatch.md`:281) — touch must be reset after the IOE is up (factory calls `ioe_tp_reset()` before `Cst820::begin`). **[V]**
* Factory touch path: polled `Cst820::read()` → `getFingerNum/X/Y` → LVGL pointer indev (`lvgl_read_cb`) **[V]**. M5Unified path: `M5.Touch.getDetail()` with its own
  flick/drag/hold state machine (§1.4) **[V]**.
* Hardware gesture IDs fire once at lift-off with fixed thresholds → feel laggy and can't drive interactive drags. **Prefer feeding raw X/Y to LVGL** (smooth
  tracking) and let LVGL derive gestures; use the chip's gestures only as low-power wake (e.g. double-tap to wake).
* **LVGL 9 gesture/indev settings** (`lv_conf.h` / runtime) **[K]**:
  - `LV_INDEV_DEF_READ_PERIOD` 10–16 ms (poll ≤ 60–100 Hz; I²C read ~0.3 ms at 400 kHz; or use IRQ-gated read).
  - `LV_INDEV_DEF_SCROLL_LIMIT` 10 px (drag threshold; raise to 12–16 on the small round screen to avoid accidental scroll); `SCROLL_THROW` 10–15 %.
  - `LV_INDEV_DEF_LONG_PRESS_TIME` 400 ms, `LONG_PRESS_REP_TIME` 100 ms; `LV_INDEV_DEF_GESTURE_LIMIT` 50 px, `GESTURE_MIN_VELOCITY` 3.
    Events: `LV_EVENT_GESTURE` + `lv_indev_get_gesture_dir()`, `LV_EVENT_LONG_PRESSED`, `LV_EVENT_CLICKED`, `LV_EVENT_DOUBLE_CLICKED` (LVGL 9.x has `lv_indev_set_*`
    runtime setters for these and `LV_EVENT_GESTURE` for four directions; 9.4+ adds multi-touch gestures `LV_USE_GESTURE_RECOGNITION`) — verify names in v9.5.0.
  - Gesture vs scroll conflict: on scrollable containers call `lv_obj_remove_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE)` appropriately; for edge swipe-back put an invisible 24–32 px
    strip object at the rim that handles `LV_EVENT_GESTURE`.
* **Double-tap** (software): two `PRESSED→RELEASED` within 250–300 ms and < 20 px apart; LVGL has `LV_EVENT_DOUBLE_CLICKED` (needs the 2nd click on the same object). Delay
  single-tap action by ~250 ms only where double-tap exists — otherwise avoid double-tap (latency) and use long-press.
* **Debounce / noise**: ignore touch within 50–80 ms after wake (resume ghost touches); reject finger-count flicker (require 2 consecutive reads to release); filter
  jitter with a 2–3 px deadzone / EMA α≈0.5 only for hold/click detection (not for drag tracking); palm rejection: ignore touches covering rim during wrist movement.
* Coordinates: confirm touch X/Y map to display (rotation/mirror); factory reads directly into LVGL with no transform **[V]** (so raw == panel orientation).
* Touch wake from display-off: IRQ on touch → read; the first touch should only wake (not activate widgets) — swallow it.

---------------------------------------------------------------------------------------------------

## 8. Reference projects

| Project | Why it's useful | Link |
|---|---|---|
| **M5StopWatch-UserDemo** (factory) | Same hardware; M5GFX Panel_AMOLED + LVGL 9.5 + mooncake + smooth_ui_toolkit; watch face, launcher, stopwatch, alarm, IMU, FFT, fluid sim | https://github.com/m5stack/M5StopWatch-UserDemo |
| **Waveshare ESP32-S3-Touch-AMOLED-1.75** | Same CO5300 466² QSPI panel + ESP32-S3R8; IDF examples: `02_lvgl_demo_v9` (sdkconfig tuned for LVGL perf), `03_esp-brookesia` (phone-style UI), `04_Immersive_block`, `05_Spec_Analyzer`; BSP component `waveshare/esp32_s3_touch_amoled_1_75` | https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75 |
| **Espressif `esp_lcd_co5300`** | Official QSPI CO5300 `esp_lcd` driver (registry v2.2.0 seen) | https://components.espressif.com/components/espressif/esp_lcd_co5300 |
| **Espressif `esp_lvgl_port`** | LVGL 8/9 port: task/timer, DMA buffers, rounder_cb, rotation, touch | https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port |
| **LVGL smartwatch demo** | `LV_USE_DEMO_SMARTWATCH` (enabled in the factory sdkconfig) — study widgets/animations | https://github.com/lvgl/lvgl/tree/master/demos (check `smartwatch`) |
| **curisama/The-Badge** | Firmware for Waveshare 1.75C round AMOLED (clock, timer, games, BLE); builds on PC too | https://github.com/curisama/The-Badge |
| **muki01/Smartwatch-ESP32** | ESP32-S3 AMOLED watch: LVGL 9 touch UI, watch faces, **always-on display**, notifications | https://github.com/muki01/Smartwatch-ESP32 |
| **fbiego/esp32-s3-amoled**, **esp32-lvgl-watchface**, **esp32-watchfaces**, **c3-mini-lvgl-template** | fbiego's ESP32 watch / LVGL watch-face projects (ESP32-S3 AMOLED focus; watchface packaging + LVGL editor flow) | https://github.com/fbiego?tab=repositories (e.g. https://github.com/fbiego/esp32-s3-amoled) |
| **linquant/jnkwatch**, **hleserg/Attadipa** | ESP32-S3 AMOLED smartwatch firmwares with LVGL | https://github.com/linquant/jnkwatch , https://github.com/hleserg/Attadipa |
| **Forairaaaaa/smooth_ui_toolkit**, **mooncake** | Spring/easing anim lib + app framework used by M5 | https://github.com/Forairaaaaa/smooth_ui_toolkit |
| **m5stack/lv_m5_emulator** | LVGL desktop emulator + the M5 LVGL port the factory copied (iterate UI on PC) | https://github.com/m5stack/lv_m5_emulator |
| **M5Dial UI demos** | 240×240 round (GC9A01) — useful UX (rotary-encoder radial menus), not same perf class | https://github.com/m5stack/M5Dial |
| **LilyGO T-Watch-Ultra / T-Display-S3-AMOLED** | M5GFX's CO5300 init list originates with T-Watch-Ultra (502×410); QSPI AMOLED + LVGL examples | https://github.com/Xinyuan-LilyGO |

(Items marked "fbiego/linquant/hleserg/muki01/curisama" were discovered via GitHub repo search; descriptions come from the GitHub API, internals not code-reviewed here.)

---------------------------------------------------------------------------------------------------

## 9. Recommended stack and rules of thumb for this project

### 9.1 Stack
1. **ESP-IDF 5.5.x** (idf.py or PlatformIO `framework=espidf`, pioarduino 55.03.312 as in the factory `platformio.ini`), target esp32s3, 16 MB flash, OPI PSRAM @ 80 MHz.
2. **LVGL 9.5.x** (same as factory) for UI; **M5GFX 0.2.19+ `Panel_AMOLED`** (copy factory `Panel_CO5300` subclass + `M5StopWatch` class from `hal_display.cpp`)
   for panel init/brightness/sleep. Optional later swap of flush to `esp_lcd_co5300` + `esp_lvgl_port` if M5GFX flush is the bottleneck (needs IOE reset/power first).
3. Custom M5GFX sprite canvases (inside an LVGL `lv_canvas`/image or a dedicated screen) only for particle/hand-heavy visuals.
4. Wake/power: use factory PowerDemo + M5PM1/M5IOE1 libs (pinned: M5IOE1 1.0.8, M5PM1 1.0.6, BMI270_BMM150_Sensor 0.1.2).

### 9.2 sdkconfig (starting point; combine factory + Waveshare tuned values)
```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_SPIRAM=y  CONFIG_SPIRAM_MODE_OCT=y  CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP32S3_DATA_CACHE_64KB=y  CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y  CONFIG_SPIRAM_RODATA=y      # only if internal IRAM/DRAM is tight; measure
CONFIG_FREERTOS_HZ=1000
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y   CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
```
### 9.3 LVGL config (`lv_conf` / Kconfig)
```
LV_COLOR_DEPTH 16                       # RGB565
LV_DEF_REFR_PERIOD 33                   # 30 fps; use 16 only for small-region interactions
LV_DRAW_SW_DRAW_UNIT_CNT 2              # one per core (set LVGL task affinity accordingly)
LV_ATTRIBUTE_FAST_MEM_USE_IRAM 1
LV_OBJ_STYLE_CACHE 1
LV_USE_OS LV_OS_FREERTOS
LV_MEM_SIZE / LV_USE_CLIB_MALLOC        # CLIB malloc (PSRAM ok) as the factory; keep layer buffers in PSRAM
LV_USE_PERF_MONITOR 1, LV_USE_SYSMON 1  # dev builds only
LV_USE_REFR_DEBUG 1                     # dev only
LV_FONT_MONTSERRAT_{16,22,28,36,48} + 1–2 custom big-digit fonts; LV_FONT_DEFAULT = MONTSERRAT_22
LV_USE_FONT_COMPRESSED 1 (optional flash saver), LV_USE_TINY_TTF only if a custom face is required
LV_CACHE_DEF_SIZE 1 MB (image cache, factory value)  LV_IMAGE_HEADER_CACHE_DEF_CNT 8
LV_USE_LOTTIE 0 (unless a specific hero animation is justified)  LV_DRAW_SW_SHADOW off or limited
LV_INDEV_DEF_READ_PERIOD 16; SCROLL_LIMIT 12; GESTURE_LIMIT 50; LONG_PRESS_TIME 400
```
### 9.4 Display buffers & flush
* **Two partial buffers, 466 × 48 RGB565 = 44 736 B each (≈ 90 KB total), `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`**, `LV_DISPLAY_RENDER_MODE_PARTIAL`.
  Fall back to 466×24 if internal heap is tight with Wi-Fi/BLE; fall back to PSRAM 466×120 (factory) only if internal RAM is unavailable.
* Fix/verify the factory's buffer-size arithmetic (`width*LV_BUFFER_LINE` passed in bytes) when porting **[?]**.
* Register `LV_EVENT_INVALIDATE_AREA` rounder (even x1/y1, odd x2/y2) **[confirm in CO5300 datasheet]**.
* Flush: `setAddrWindow` + DMA push (`pushPixelsDMA`/`writePixels` with swap565) → async; `lv_display_flush_ready()` after DMA completes; don't call from
  the task that renders the next chunk without waiting on `waitDMA()`.
* TE: attach ISR on G38 (input, rising edge) → `xSemaphoreGiveFromISR`; wait (timeout 20 ms) only before the first chunk of a refresh when invalidated area > ~25 % of
  screen. Keep `0x35` TE on and `0x44 01 D2` as factory. Measure tearing with a moving bar.
* LVGL task: priority 2–4 on **core 1** (leave core 0 for Wi-Fi/system), stack ≥ 8 KB internal (factory 16 KB), mutex or `lvgl_port_lock`; tick via
  `lv_tick_set_cb(esp_timer-based)`; use `lv_timer_handler()` return value for sleep, clamp 1–33 ms while animating, ≤ 500 ms idle.

### 9.5 Targets & budgets
| Item | Target |
|---|---|
| Interaction animation | 30 fps steady; ≤ 33 ms frame (render+flush overlapped) |
| Small-region effects (arc, hand, spinner ≤ 200×200) | 60 fps if render ≤ 8 ms |
| Full-screen transition (`lv_screen_load_anim` MOVE) | 250–300 ms, ease-out, ≥ 20 fps acceptable |
| Idle | 0 fps; LVGL sleeps; display brightness ≤ 30 % after 10 s |
| Latency tap → visual feedback | < 100 ms (press-state style change on `PRESSED`) |
| Internal RAM for UI | ≤ ~150 KB (buffers ~90 KB + stacks + LVGL heap in PSRAM) |
| PSRAM for UI | ≤ ~2 MB (image cache 1 MB + layers) |

### 9.6 Design rules of thumb
1. Black background, one accent colour per screen, state colours green/amber/red; never white fills.
2. Font sizes: body 22–28 px, secondary ≥ 18 px, hero 48–96 px; min weight Medium; no text < 16 px; digits tabular.
3. Touch targets ≥ 80 px (target 96); keep interactive content inside r ≤ 190 px; rim 24–32 px strip = system gestures (back / page swipe).
4. Animations 150–300 ms, ease-out in / ease-in out, overshoot only on confirm; animate transforms of small opaque objects, avoid opacity on big areas.
5. Haptic 10–30 ms tick on snap/confirm, double pulse on error; fire at impact; throttle ≥ 80 ms.
6. Dim → AOD (outline UI, ≤ 10 % lit pixels, 1-minute updates, ±2 px pixel-shift) → display off; wake by touch/button/BMI270 wrist-raise; ramp brightness on wake.
7. Prototype UI on PC with `lv_m5_emulator`; profile on-device with `LV_USE_PERF_MONITOR` + `REFR_DEBUG` before adding any effect; measure PSRAM vs SRAM buffer fps
   (run `LV_USE_DEMO_BENCHMARK`, already compiled in the factory sdkconfig).

---------------------------------------------------------------------------------------------------

## 10. Gaps / things to verify on hardware
* No TE-gated flush found in factory `hal_display.cpp` (only that file reviewed; `Panel_AMOLED.inl` was only keyword-scanned). Confirm whether M5GFX waits on TE internally.
* Whether the CO5300 on StopWatch truly needs 2-pixel aligned windows (M5GFX hides this inside `setWindow`) — read `docs/datasheets/CO5300_display_driver.pdf`.
* LVGL 9.5 names for ESP32-S3 asm blend and lottie (`LV_USE_LOTTIE`), gesture settings — check `lv_conf_template.h` of v9.5.0. I did not fetch LVGL docs (the docs URL tried returned 404).
* Idle/AOD register behaviour (0x38/0x39, partial area) of this panel is untested; M5GFX source itself notes "more testing needed".
* Performance numbers in §3.1 are derived/typical, not measured on this board.
* CST820B gesture register/IDs need datasheet confirmation (`docs/datasheets/CST820B_touch.pdf`).
* Not reviewed: factory `app_launcher/view`, `app_watch_face` code (good next read for the transition and animation patterns used by M5), `smooth_ui_toolkit` API.
