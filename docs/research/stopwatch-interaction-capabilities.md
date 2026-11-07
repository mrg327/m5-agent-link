# StopWatch interaction capabilities and constraints

Scope: what the M5Stack StopWatch (C152, ESP32-S3R8) and **our current firmware** can support for a "select a session, then act with one gesture" UI. Read-only investigation; the hardware was not touched. Nothing here was measured on the device unless stated.

Conventions: `file:line` refers to this repo (paths relative to repo root; `fw/` = `firmware/`, `core/` = `firmware/components/core/src/`, `board/` = `firmware/boards/stopwatch/`). URLs are datasheets or vendor docs. **[unverified]** marks anything that was not confirmed from a primary source or on the device. **[estimate]** marks my arithmetic or judgement.

Other docs touching this area: `docs/research/minimal-peripherals.md`, `docs/research/wearable-glance-gestures.md`, `docs/research/ui-animation.md`.

## 0. Key findings

1. **The touch panel is polled, single-point, ~100 Hz, and we only use x/y/finger-count.** The CST820 datasheet claims a >100 Hz refresh and "single-point gestures plus true two-point operation" (`docs/datasheets/CST820B_touch.pdf` p.1). Our driver reads one point (`board/touch.cpp:58-68`). The INT pin (G13) is defined but never used (`board/board_priv.h:14`).
2. **Our gestures are all derived in software, on release.** The chip's own gesture byte is ignored. Swipes and taps only fire when the finger lifts (`core/gesture.c:27-56`), so there is no live drag or angle stream yet. A ring "scrub" needs a new recognizer (small change).
3. **Buttons A and B are plain GPIOs (G2/G1), debounced 12 ms in software.** Press, hold (600 ms) and release are detectable. Double-press and chords are not implemented but are cheap. The third button (PWR) belongs to the M5PM1 and double-click powers the device off, so it is not usable.
4. **The IMU (BMI270) has no driver in our firmware.** `has_imu=false` (`board/board.cpp:71`) and `hal_motion_set_callback` stores the callback and never fires it (`board/board.cpp:180-186`). The BMI270 has hardware lift/flick/jiggle features but **no tap or double-tap engine**, so a "knock" must be done in software from the accelerometer stream.
5. **No audio yet.** `hal_sound_play` is a stub (`board/board.cpp:193-197`). Hardware exists (ES8311 + AW8737A amp + 1 W speaker + MEMS mic).
6. **The green LED is a single on/off PM1 pin, currently used as a "display is off" indicator** (`board/display.cpp:170,271,277`).
7. **A touch cannot wake a sleeping display today.** In standby the touch chip is put into sleep (`board/board.cpp:127`, `board/touch.cpp:69-77`) and the touch read callback reports "released" while the panel is unpowered (`board/display.cpp:214`). Only button releases (and link attention) wake it (`core/app.c:197`).
8. **Screen cost is dominated by full-screen invalidation.** The ring widget is a 466x466 object (`core/ui_ring.c:54`), so any ring change repaints the whole screen. No fps was measured; the fps counter exists (`core/app.c:311-315`, `core/debug.c:78`) but no recorded value was found in the repo.

## 1. Inputs

### 1.1 Touch (CST820 / CST820B)

| Item | Fact | Source |
|---|---|---|
| Chip | CST820(B) self-capacitive, I2C addr 0x15, INT G13, reset via IOE1 PYG4; chip id read back as 0xB7 | `docs/StopWatch.md` "Touch"; `board/touch.cpp:12`; session checkpoint 003 |
| Report rate | ">100 Hz" refresh | `docs/datasheets/CST820B_touch.pdf` p.1 |
| Points | "single-point gestures and true two-point operation" (datasheet claim). Our reader decodes 1 point only (7-byte read from reg 0x00) | datasheet p.1; `board/touch.cpp:58-68` |
| Sensor | up to 17 sense channels, self-cap, "TP size recommended within 2 inch" | datasheet p.1 |
| I2C | 10-400 kHz; we run the device at **100 kHz** on the shared SYS bus (G47/G48) | datasheet p.7; `board/touch.cpp:36` |
| Modes | Dynamic (fast scan) -> auto Standby after 2 s without touch (low-rate scan, wakes the host by INT) -> Sleep on command (reset pin to wake). Auto-standby can be disabled by register | datasheet p.6 |
| Hardware gestures | The datasheet text mentions "standby gesture wake" but gives **no register map**. By analogy with the CST816 family, byte 0x01 of the report is a gesture id (swipe up/down/left/right, click, double click, long press) and register 0xFA/0xEC configure IRQ/motion. **[unverified for CST820]** | datasheet p.1,6; community CST816S docs |
| What we use | Only finger count (b[2]), event flag (b[3]>>6) and X/Y (b[3..6]). The gesture byte b[1] is read but ignored | `board/touch.cpp:62-66` |
| Sampling in our firmware | LVGL indev read timer set to **10 ms** (`core/app.c:283`). `core_step` never sleeps more than 10 ms (`core/app.c:316`; `main/main.c:35-36`). Effective ceiling is ~100 Hz, no INT-driven read | as cited |
| I2C failure behaviour | A failed read returns "not pressed" (`board/touch.cpp:58-60` -> `board/display.cpp:214-219`), which looks like a lift. A transient bus error mid-hold would therefore cancel a hold-to-confirm | as cited. Likelihood **[unverified]**; the bus is shared with PM1/IOE1 haptic writes |

**Latency (finger -> app event)** [estimate]: chip scan <=10 ms + our poll <=10 ms + one `core_step` (gestures are popped after `lv_timer_handler`, `core/app.c:303-306`) = roughly 10-25 ms for G_PRESS. Visual feedback then waits for the next LVGL refresh tick (`LV_DEF_REFR_PERIOD=33` ms, `fw/sdkconfig.defaults:29`) -> 30-60 ms touch-to-pixel. Taps are recognised on release; taps that navigate are additionally deferred by 330 ms (section 3.2).

**Jitter / slop.** `GESTURE_TAP_SLOP_PX = 18` (raised from 12; `core/gesture.h:11`). The finger is "moved" when either axis exceeds the slop relative to the press point (`core/gesture.c:70`). Actual noise was not measured; self-cap panels typically give +-1-3 px at rest **[unverified]**.

**Multi-touch.** Not available to us today (single-point reader). The datasheet's "true two-point" is a feature of the chip, but self-capacitive two-point has ghosting ambiguity and we have no register documentation, so treat as **not possible for planning**.

**Edge / bezel use for a "ring scrub".**
- The glass is round but the sensor is a rectangular-ish self-cap pattern; the vendor's own firmware uses "swipe from the top edge toward the centre" to show the battery (`docs/guide/usage.md:19`), so touches near the rim are reported. Accuracy at the very rim (outer ~15 px) **[unverified]**: expect compression/clamping; the driver code already clamps to 465 (`board/display.cpp:215-216`).
- The physical bezel is not touch-sensitive. Treat the usable annulus as r ~ 170-225 px from centre (233,233). Our existing glance tap already uses `dist >= 150 px` as "on the ring" (`core/ui_ring.c:85`).
- **Angular resolution at r = 200 px** [estimate]: 1 px of arc = 360/(2*pi*200) = **0.29 deg**. Rest jitter of +-2 px ~ +-0.6 deg. A 10 deg step = 35 px of arc; the existing 60 px swipe threshold (`core/gesture.h:9`) = 17 deg. At 100 Hz sampling a 1 rev/s circular swipe moves 12.6 px (3.6 deg) per sample, which is easily tracked. With N sessions on the ring, sector width is 360/N: N=8 -> 45 deg (157 px of arc), N=16 -> 22 deg (78 px). Sessions up to `RING_MAX_SEGMENTS = 32` (`core/ui_ring.h:6`) would be 11 deg (39 px), marginal for finger selection but fine for "scrub with detents".
- Practical design: recognise the finger's angle relative to the centre and emit a continuous angle delta (with hysteresis ~ 1/3 of a sector) instead of requiring a completed swipe. Needs a new drag event stream (small change, section 5).

### 1.2 Buttons

| Item | Fact | Source |
|---|---|---|
| Buttons | KEYA (yellow) = **G2**, KEYB (blue) = **G1**, active low, internal pull-ups; read as ESP32 GPIOs (not via PM1 or IOE1) | `docs/StopWatch.md` "KEY"; `board/board_priv.h:15-16`; `board/input.cpp:19,128-134` |
| Power button | Third button on the M5PM1 (PWR_BTN). Single click = power on/reset, **double-click = power off**, long hold = download mode. We configure PM1 button to CLICK with a 1000 ms delay and disable single-click reset | `docs/StopWatch.md` "Power On/Off"; `board/sw_power/sw_power.cpp:55,63` |
| Wake | Schematic notes "KEY*3; Wake-up support" | `docs/hardware/StopWatch_schematic.pdf` (text layer) |
| Read path | GPIO any-edge ISR -> task notify -> task waits **12 ms** debounce -> reports changed levels (`BTN_DEBOUNCE_MS`) | `board/input.cpp:17,35-57` |
| Events out | `hal_button_cb_t(btn, DOWN/UP, t_us)` raw edges; ring buffer of 16 | `agentlink_hal/include/hal.h:63-69`; `core/app.c:31,69-73` |
| What the recogniser does | Emits `G_BTN_A/B` **on release** if no long press fired; emits `G_BTN_A/B_LONG` at 600 ms while still down (`GESTURE_LONG_PRESS_MS`). No double-press, no chord | `core/gesture.c:76-102`, `core/gesture.h:8` |
| Latency | edge -> debounce 12 ms -> drained within <=10 ms by `core_step` = **~12-25 ms** [estimate] | as cited |
| Press / hold / double / chord | Press: yes. Hold: yes (600 ms, configurable). Double: not implemented, trivial (same pattern as touch double-tap). Chord: both `btn[i].down` flags are already tracked (`core/gesture.h:39`); a chord recogniser is a few lines. The factory firmware itself uses "hold A+B together" to return to the menu (`docs/guide/usage.md:11,19`) | as cited |
| Display-off | Any button release wakes the display and is **consumed** (`power_activity` returns true -> `dispatch` returns) | `core/app.c:197-200`, `core/power.c:40-50` |

### 1.3 IMU (BMI270) and interrupts

| Item | Fact | Source |
|---|---|---|
| Chip | **BMI270**, I2C 0x68 on the shared SYS bus (the M5Unified table lists 0x69 for some boards; the StopWatch page says 0x68) | `docs/StopWatch.md` "IMU & RTC"; `docs/github/M5Unified.md:446` |
| Interrupt routing | One net `PMG0 (RTC_INT & IMU_INT)` goes to the **M5PM1** GPIO0 (wake/IRQ). PM1's `PMG1 (G12_PY_IRQ)` is the interrupt line to the ESP32-S3 (G12). So the ESP32 does not see the IMU INT directly; the path is IMU -> PM1 GPIO0 (IRQ function) -> PM1 IRQ status -> G12. The ESP can always poll the BMI270 over I2C instead | `docs/StopWatch.md` "M5PM1" table; schematic text (`PMG0_RTC&IMU_INT`, `G12_PY_IRQ`). Whether INT1 or INT2 is wired **[unverified]**; the TODO in our code agrees with the path (`board/sw_power/sw_power.cpp:64`, `board/board.cpp:184`) |
| Our firmware | No driver. `has_imu=false`, motion callback never fires. `gesture_kind_t` already reserves `G_LIFT` and `G_SHAKE` and `app.c` already routes them (SHAKE -> glance, LIFT ignored) | `board/board.cpp:71,180-191`; `core/gesture.h:19`; `core/app.c:176-181,216-220` |
| Available code | The vendor power demo vendors an `otool_bmi270` snapshot and has working "IMU wake" profiles (accel + gyro + wrist-wear wake-up + any-motion; or accel + any-motion only) at 671 uA / 38 uA in L1 standby | `docs/github/M5StopWatch-PowerDemo.md:24-27,101`. Alternatives: M5Unified `IMU_Class` (Arduino; we do not use it), Bosch BMI270 SensorAPI [unverified licence/size] |
| Init cost | BMI270 needs the ~8 KB feature config blob burst-written to INIT_DATA (reg 0x5E) between `INIT_CTRL=0` and `INIT_CTRL=1`, after disabling advanced power save, before the feature engine works. Whether plain accel/gyro readout needs it **[unverified]**; plan for it | https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf (4.4 init sequence, 4.8) |
| Hardware features | Significant motion, any-motion, no-motion, motion/stationary detect, **wrist wear wake-up (raise to look)**, step counter/detector, activity change, **wrist gestures: flick in, flick out, push-arm-down, pivot-up, wrist jiggle/shake**, orientation, flat. The datasheet's feature list and section index contain **no tap / double-tap** detector | same datasheet, Key features + 4.8.x |
| Hardware caveat | Wrist-wear wake-up and the wrist gestures assume a **wrist-worn watch** in a defined axis frame (left/right arm config, `WR_GEST_1.wearable_arm`, axis remap); the StopWatch is a lanyard/desk device, so thresholds/axis remap need tuning and some may not fit **[unverified on device]** | same datasheet 4.8.6-4.8.7 |
| Raw rates | Accel ODR 12.5-1600 Hz (normal) / 0.78-400 Hz (low power); gyro 25-6400 Hz; 2 KB FIFO; +-2/4/8/16 g | same datasheet, spec tables |
| Power | Vendor measurement: IMU sampling (acc+gyr+AUX continuously, screen off) 36.3 mA total system; "Normal operation" with IMU initialised but idle 45.7 mA; IMU-wake L1 low-power mode (accel + any-motion) 37.6 uA | `docs/github/M5StopWatch-PowerDemo.md:24-27,32,34` |

### 1.4 Anything else

- **No crown, encoder or rotary input.** Inputs are: touch, KEYA, KEYB, PWR (PM1), IMU.
- **Microphone**: MEMS mic through the ES8311 codec (I2S), power via IOE1 PYG3 (`docs/StopWatch.md` "Audio"). No firmware. Not needed for this UI; a "blow/clap" input would be a new-driver, unreliable idea.
- **RTC RX8130CE** (0x32) is used by us (`board/rtc.cpp`) and shares the PMG0 interrupt line with the IMU.
- **Other pins**: HY2.0 PORT.A (G10/G11), rear 2.54 bus; irrelevant to interaction.

## 2. Outputs

### 2.1 Display

| Item | Fact | Source |
|---|---|---|
| Panel | 1.75" round AMOLED, **466x466**, CO5300 controller over QSPI (SCLK G40, D0-3 G41/42/46/45, CS G39, TE G38), reset via IOE1 PYG5, power rail IOE1 PYG8 | `docs/StopWatch.md` "AMOLED"; `board/display.cpp:20-26` |
| Controller facts | Native 60 Hz (TE spec "Frame Rate = 60Hz"); full-colour 16.7 M, 262 K, 65 K; **Idle mode** with 16.7 M / 4096 / 8 colours (AOD-style); 8-bit brightness reg 0x51, HBM regs 0x63-0x66; GRAM 480x480; gamma per RGB | `docs/datasheets/CO5300_display_driver.pdf` features (lines ~285-290, 7.5.30-31, 7.5.46-49) |
| QSPI clock | 80 MHz write (`cfg.freq_write = 80000000`) | `board/display.cpp:77` |
| Init | Colour mode reg 0x53=0x20, brightness 0xA0, TE at line 466, panel offset x=6 | `board/display.cpp:44-52,97-98` |
| Framebuffer | Full-screen PSRAM framebuffer via M5GFX `Panel_AMOLED_Framebuffer` with auto-display; LVGL partial flushes become 2-px-aligned QSPI pushes (434 KB RGB565) | `board/display.cpp:108-116` |
| Colour depth | LVGL RGB565 (`LV_COLOR_DEPTH_16`) end to end -> 5/6/5 bits per channel, i.e. visible banding on slow gradients | `fw/sdkconfig.defaults:28`; `board/display.cpp:198` |
| LVGL | **9.5.0** (`fw/dependencies.lock`; `managed_components/lvgl__lvgl/idf_component.yml:9`). SW renderer, 1 draw unit, no asm accel, no gradients, **no vector/float**, complex draw (arcs, AA) on; `LV_DEF_REFR_PERIOD=33` (30 fps cap), `LV_DPI_DEF=130`; clib malloc (so LVGL objects use the IDF heap; allocations >16 KB go to PSRAM) | `fw/sdkconfig:2776-2826,2889`; `fw/sdkconfig.defaults:12-40` |
| Fonts | Montserrat 14, 16, 20, 22, 24, 28, 32, 48 only (no 12/10, no CJK/emoji) | `fw/sdkconfig:2902-2921` |
| Draw buffers | Two partial buffers of 466 x 48 lines x 2 B = **44.7 KB each, internal DMA RAM** (PSRAM fallback), `RENDER_MODE_PARTIAL` -> ~10 flush chunks per full screen; flush writes in 8192-px chunks under a mutex | `board/display.cpp:28-29,181-200,133-154` |
| Rounder | Even-aligned areas required by the CO5300 | `board/display.cpp:157-166` |
| FPS | Cap is 30 fps by LVGL period. **Achieved fps not measured/recorded**; the counter counts `LV_EVENT_REFR_READY` per second (`core/app.c:95-99,311-315`) and is exposed in `dbg_ui_state` (`core/debug.c:78`). Earlier research expects QSPI line rate ~55-70 fps raw and LVGL SW full-screen alpha content at 8-25 fps **[K/?, unmeasured]** | `docs/research/ui-animation.md:119-131`; `docs/design/FIRMWARE.md:87` |
| Rotation | `hal_display_set_rotation_180` via LVGL; USB at the bottom = upright | `board/display.cpp:282-287`; checkpoint 003 |

### 2.2 AMOLED power and burn-in (always-on coloured ring)

- Vendor measurements: 100 % white static frame = **154 mA**; "normal active" at 50 % brightness with a 1 Hz-refresh UI = **45.7 mA** (display + CPU + sensors); Wi-Fi-connected idle with display off ~ 3.5-105 mA depending on sleep (`docs/github/M5StopWatch-PowerDemo.md:32-37`). Battery is 450 mAh (`docs/StopWatch.md`). So an always-on **dark** UI with a thin ring is cheap; a bright full-screen colour is not (AMOLED power scales with lit area x brightness x colour; blue/white cost most **[general knowledge]**).
- Our UI is already AMOLED-black (`COL_BG 0x000000`, `core/ui_theme.h:6`); the glow ring dims to ~55 % (`core/ui.c:181`). Display brightness is runtime-adjustable 0-255 (`board/display.cpp:240-251`, default 160 = ~63 %).
- The display idles off after a timeout (`core/power.c:78`) and the rail is cut (`board/display.cpp:272-278`); re-wake costs ~20+10+20 ms of rail/reset delays plus init commands and a full repaint from the PSRAM framebuffer (`board/display.cpp:259-270`), i.e. **wake is ~100+ ms** [estimate].
- **Burn-in**: a fixed-position, fixed-colour ring (10 px band at r=230, `core/ui_ring.c:7-9`) is the textbook burn-in pattern for OLED. Mitigations to plan: dim the always-on ring (<=30-40 % luminance), shift/rotate its start angle slowly (sub-pixel pixel-shift or an hourly rotation), rely on state-driven colour change (segments change as sessions change), and turn the display off when nothing needs attention. Panel burn-in ratings are not published in the datasheets we have **[unverified]**.
- Idle mode (8-colour AOD) exists in the CO5300 (`0x39`) and could host an ultra-low-power ring, but our M5GFX framebuffer path does not use it and it would need an on-device test **[unverified]**.

### 2.3 Vibration motor

| Item | Fact | Source |
|---|---|---|
| Hardware | ERM motor driven by M5IOE1 PYG9 PWM channel 0 through a MOSFET | `docs/StopWatch.md` "M5IOE1"; schematic text (MOTOR/MOS) |
| PWM | Frequency set to **5 kHz** (shared by all IOE1 PWM channels); duty is 12-bit in the library (0-4095) but our wrapper uses the **0-100 % API** (1 % steps) | `board/sw_power/sw_power.cpp:108`; `managed_components/m5stack__M5IOE1/src/M5IOE1.h:1051,1075`, `M5IOE1.cpp:2505-2514`; `sw_power.h:34` |
| Amplitude | Fixed: hold 85 %, 15 ms kick at 100 % for pulses >= 30 ms. "The motor does not start below ~25 %" | `board/input.cpp:15-19,111-118` |
| Pattern format | `hal_haptic_play(on_off_ms[], count)`: alternating on/off durations, starting with on; **<= 32 steps**; non-blocking; a new pattern replaces the running one; count 0 stops | `agentlink_hal/include/hal.h:89-93`; `board/input.cpp:17,66-84` |
| Timing resolution | 1 ms nominal (FreeRTOS tick 1000 Hz, `fw/sdkconfig.defaults:21`), plus ~0.3-1 ms I2C per duty write [estimate]. Perceptible floor is the motor spin-up: a 12 ms tick was "likely too short to feel" (checkpoint 003), current minimum is 35 ms | `core/feedback.c:30` |
| Current patterns | Needs input/approval/question 250-120-250 ms; error 80-70-80-70-80; done 80; tick 35; confirm 45-50-45; reject 35-60-35; hello 60 | `core/feedback.c:17-19,30,36,42,48` |
| Gating | Alert mode SILENT suppresses haptics (except hello); FULL also enables sound | `core/feedback.c:5-13` |
| Limits for redesign | No per-step amplitude and no ramp; "progress while holding" haptics must be built from pulse trains (e.g. pulses whose gap shrinks with progress), or by extending the HAL with a duty argument (small change). Motor shares the I2C bus with touch/IMU/PM1 (mutex serialised) so continuous PWM updates can delay touch reads **[unverified]** | `board/sw_power/sw_power.cpp:139-148` |

### 2.4 LEDs

- **PM1 status LED** ("green LED" near USB): controlled by `M5PM1::setLedEnLevel(bool)` which toggles the LED_CTRL bit of the PM1 power config register (`managed_components/m5stack__M5PM1/src/M5PM1.cpp:3358-3362`); wrapper `sw_pm1_led(bool)` (`board/sw_power/sw_power.cpp:67-75`). **On/off only: no brightness, no colour, no PWM.** Whether this is the same LED that blinks in download mode and whether the charger drives it in hardware **[unverified]**; the schematic names `PM_STATUS_LED` / `LED_EN_PP`.
- Today the LED is OFF while the display is lit and ON while the display is off ("the AMOLED is the power indicator", `board/display.cpp:170,271,277`). This makes it a free "device is asleep" or attention blinker; it must not be re-purposed without keeping the "display-off" signal in mind.
- PM1 also supports NeoPixel on GPIO0 (`M5PM1.h:207`) but on the StopWatch PMG0 is the RTC/IMU interrupt, so **no addressable LEDs**.

### 2.5 Speaker and buzzer

- Hardware: ES8311 codec (I2C 0x18, shares SYS bus; I2S MCLK G18, BCLK G17, DOUT G16, LRCK G15, DIN G21), AW8737A amp enabled by IOE1 PYG10 (+ G14 `SPK_EN` per schematic), 1318 cavity speaker 8 ohm 1 W, audio rail IOE1 PYG3 (`docs/StopWatch.md` "Audio"; `board/sw_power/include/sw_power.h:10-20`). There is **no separate buzzer**.
- Firmware: `has_speaker=false`; `hal_sound_play` is an empty TODO (`board/board.cpp:73,193-197`); IOE rails for audio are driven low at boot (`board/sw_power/sw_power.cpp:104,107`). The core already has three sound ids and gates them to ALERT_FULL (`agentlink_hal/include/hal.h:95-96`; `core/feedback.c:10-13,22-24`).
- Effort to enable (**new driver**): I2S + ES8311 init (the power demo vendors `esp_codec_dev` 1.6.2 for this, `docs/github/M5StopWatch-PowerDemo.md:102`), enable PYG3 then PYG10, play short synthesised tones (a few KB, no assets needed), power the amp only during playback. Internal-RAM cost for I2S DMA is small (a few KB) **[estimate]**.

## 3. Current UI and gesture code (what the redesign replaces)

### 3.1 Screens

`ui_init` builds three screens plus two overlays (`core/ui.c:16-18,40-80`). Navigation uses `lv_screen_load_anim` slides of 250 ms (`core/ui.c:13,112-125`).

| Screen | File | Content |
|---|---|---|
| **glance** | `core/ui_glance.c` | Clock, hero text, caption, running/done counters, link indicator, "swipe up" hint, and a **segmented colour ring** with one arc per session (state colour from `theme_state_rgb`); pulsing dot animation 900 ms ease-in-out (`core/ui_glance.c:27-38,105`) |
| **list** | `core/ui_list.c` | Vertical list, 88 px rows (`ROW_H`), 220 ms eased scroll animation (`SCROLL_MS`); selected row centred |
| **detail** | `core/ui_detail.c` | Session card / **attention card**: agent, title, state chip, preview, **Approve** (hold bar), **Deny**, **Open on PC** buttons |
| overlay: toast | `core/ui.c:167-177` | 1800 ms toast pill at y=366 (`TOAST_MS`) |
| overlay: glow | `core/ui.c:179-187`, `core/power.c:52-62` | Display-off attention: wake panel, show a dim state-coloured ring for 2 s (`GLOW_MS`), then sleep again |

Ring widget: 10 px arc band at outer radius 230, 4 deg gap between segments, up to 32 segments (`core/ui_ring.c:7-9`, `core/ui_ring.h:6`). It draws via `lv_draw_arc` in a `DRAW_MAIN` callback and invalidates its whole 466x466 object on any colour change (`core/ui_ring.c:54,72`). `ring_hit` maps a tap to a segment (angle from 12 o'clock, radius >= 150 px) (`core/ui_ring.c:80-90`).

Palette: Primer-dark inspired; amber 0xd29922 (attention), red 0xf85149 (error), green 0x3fb950 (done), blue 0x58a6ff (accent/toast), grey 0x6e7681 (idle), purple 0xa371f7 (cloud) (`core/ui_theme.h:12-17`; attention colours at `core/app.c:147`).

### 3.2 Gesture -> action map as implemented

Recogniser parameters (`core/gesture.h:7-11`): double-tap window **300 ms** (and <= 40 px between taps, `core/gesture.c:45`), long press **600 ms** (only if the finger has not moved beyond slop, `core/gesture.c:92`), swipe >= **60 px** within **400 ms** (dominant axis, `core/gesture.c:36-38`), tap slop **18 px**. Raw `G_PRESS`/`G_RELEASE` are also emitted for hold-to-confirm (`core/gesture.c:66,31`).

**Double-tap deferral.** A `G_TAP` on a screen where a tap would navigate is held back: `ui_tap_defers` is true for every tap on *detail*, and on *list* when the tap hits the already-selected row (`core/ui.c:208-212`). It is flushed after `GESTURE_DOUBLE_TAP_MS (300) + DEFER_SLACK_MS (30) = 330 ms` if the finger is up and no second tap arrived (`core/app.c:39,222-223`). A second tap becomes `G_DOUBLE_TAP`, cancels the deferred tap (`core/app.c:201`), and any other non-raw gesture flushes it immediately (`core/app.c:202`). So **single-tap actions on detail and on a selected list row cost +330 ms latency**; taps on the glance ring and on unselected list rows are immediate.

First touch/button with the display off only wakes it and is dropped (`core/app.c:197-200`).

| Screen | Gesture | Action | Source |
|---|---|---|---|
| glance | tap on ring segment | select that session -> detail | `core/ui_glance.c:124-128` |
| glance | tap elsewhere / swipe up / **A** | -> list | `core/ui_glance.c:129-137` |
| list | swipe up / swipe down | next / previous row (down at row 0 -> glance) | `core/ui_list.c:157-161` |
| list | tap unselected row | select row | `core/ui_list.c:166-167` |
| list | tap selected row / **A** | open detail | `core/ui_list.c:168-171,180-185` |
| list | **double-tap** row | **focus terminal on PC** | `core/ui_list.c:174-178` |
| list | swipe left / **B** | -> glance | `core/ui_list.c:186-187` |
| detail | press inside Approve + hold 1200 ms | **approve** (progress bar fills; release cancels; sends measured `hold_ms`) | `core/ui_detail.c:121-135,106-116`; `core/ui_internal.h:12` |
| detail | tap Deny | **deny** | `core/ui_detail.c:137-140` |
| detail | tap "Open on PC" / double-tap anywhere / **A** | **focus terminal** | `core/ui_detail.c:141-143,145,152` |
| detail | swipe right | **ack** ("done") then back to list | `core/ui_detail.c:146-149` |
| detail | swipe left / **B** | -> list | `core/ui_detail.c:150-151` |
| any | **B long (600 ms)** / shake (not produced yet) | -> glance | `core/app.c:177-179` |
| any | LIFT | ignored | `core/app.c:176` |
| link event | attention (approval/question/error/done) | haptic + glow if display off; auto-open the session unless busy elsewhere or kind = done | `core/app.c:145-155` |

Actions go out as `LINK_OP_FOCUS/APPROVE/DENY/ACK` (`core/app.c:109-137`). The daemon enforces `hold_ms >= MIN_APPROVE_HOLD_MS = 1000` for approve (`daemon/crates/agentlinkd/src/device.rs:17,333`; `docs/design/PROTOCOL.md:151`). The firmware hold is 1200 ms, so a ~200 ms margin remains for dispatch/timer skew. Note the hold timer starts when `G_PRESS` is *dispatched* (after `lv_timer_handler`, `core/ui_detail.c:124`), which is <=~25 ms after the real touch; the daemon-side check is therefore never optimistic. A drift out of the button while holding does **not** cancel; only a release does (`core/ui_detail.c:128-132`).

Snooze does not exist in the firmware or action set today (`ui_actions_t` = focus, approve, deny, ack, tick; `core/app.c:139`).

Existing gesture tests: simulator scripts (`dtap_focus.txt`, `tap_deferred.txt`, `link_indicator.txt`) per checkpoint 003; the recogniser is pure C with an explicit clock, so unit tests are straightforward (`core/gesture.h:1-3`).

## 4. Constraints for a one-screen ring/orbit UI

### 4.1 Memory

From `idf_size.py` run read-only on the existing `firmware/build/agentlink.map` (built 14:51 today, includes BLE):

| Region | Used | Free | Total |
|---|---|---|---|
| DIRAM (static: text+bss+data) | 206 615 B (60.5 %) | **135 145 B** | 341 760 B |
| of which .bss / .text / .data | 91 448 / 96 175 / 18 992 | | |
| IRAM | 16 384 B (100 %) | 0 | 16 384 B |
| Flash code / rodata | 764 450 / 355 004 B | | image 1 251 037 B |

The previous USB-only map (`build-usb/agentlink.map`) shows DIRAM 185 915 B used, so BLE cost about +20.7 KB of static DIRAM in that comparison (the +57 KB figure from checkpoint 003 includes runtime heap, which a map cannot show). **Static free DIRAM (135 KB) is not free heap**: the two LVGL draw buffers take 2 x 44.7 KB = ~89 KB of internal DMA RAM at runtime (`board/display.cpp:28,181-183`), leaving roughly **~45 KB** for stacks (btn/haptic 3 KB each, main task 8 KB, NimBLE/USB tasks), FreeRTOS objects, the LVGL heap allocations <= 16 KB, and BLE internal buffers [estimate; no runtime heap print exists in the code]. `.bss` also holds the 8 x ~4 KB RX frame pool (32 KiB, `core/app.c:26-28`).

Implications:
- Do **not** add large internal buffers. Anything big (canvas, image caches, snapshots) must be PSRAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` sends allocations >16 KB there, `fw/sdkconfig.defaults:17`). PSRAM is plentiful: 8 MB, of which the panel framebuffer uses ~0.43 MB.
- An IMU driver adds small RAM (config blob is 8 KB in flash, streamed to the chip, not held in RAM) plus a task stack (~3 KB). A sound driver adds I2S DMA buffers (few KB).
- Reducing the draw-buffer height (e.g. 48 -> 32 lines) would free ~30 KB internal but costs more flush chunks.
- A runtime `heap_caps_get_free_size` log (internal / DMA / PSRAM) was not found in code; add one to the debug channel before committing to the budget **(recommended; not done here)**.

### 4.2 Rendering and animation cost

- Partial-render with two 48-line buffers; a full-screen repaint = ~10 render+flush chunks, flush mutex-serialised with `hal_display_*` calls (`board/display.cpp:133-154`). The framebuffer is in PSRAM, so each flush is PSRAM-write + QSPI.
- LVGL SW renderer only (no PPA/GPU on ESP32-S3), single draw unit, AA arcs. Every animated ring change currently invalidates the **entire 466x466 ring object** (`core/ui_ring.c:54,72`), i.e. a full-screen repaint, even though the band is 10 px wide. For smooth orbit animation (rotating highlight, pulsing segment), **split the ring into narrow invalidation regions** (per-segment child objects or invalidate only the touched arc bounding box) before animating. This is the single most important performance fix for a ring UI.
- Fps ceiling = 33 ms refresh period (30 fps) (`fw/sdkconfig.defaults:29`). Realistic budget for animated full-screen content is unmeasured; existing screen transitions use edge-to-edge slide copies deliberately (`core/ui.c:119`), and the existing pulse animation runs on a small dot.
- Colour animation on AMOLED is cheap if limited to a thin ring and small centre region; avoid large alpha blends, shadows (`SHADOW_CACHE_SIZE=0`), gradients (disabled), and rotated images (no transform acceleration).
- Fonts are limited to 8 Montserrat sizes. Text is ASCII/LVGL symbol only.
- `LV_USE_FLOAT` is off, so `lv_draw_arc` angles are integers (`core/ui_ring.c:44-45` casts to `lv_value_precise_t`): arc end angles move in whole degrees; fine for the ring.
- Touch-to-hold-fill feedback runs from `ui_tick` once per `core_step` (<=10 ms), but pixels only update at the 33 ms refresh.

### 4.3 Other constraints

- Single UI thread: HAL callbacks only enqueue; `core_step` is the only place that touches LVGL (`core/app.c:1-2`). New input sources (IMU, drag stream) must follow the same ring-buffer pattern (`core/app.c:25-33,75`).
- Input rings are small: gestures 32, buttons 16, motion 8 (`core/app.c:263-266`). A 100 Hz drag stream must be coalesced (e.g. keep only the latest point) to avoid starving other gestures.
- With the display off the touch controller is asleep (see 0.7). "Tap to wake" would need the touch chip left in standby (INT on G13, ~10 uA per datasheet) plus a GPIO wake/INT handler, at the expense of power; currently only buttons/attention wake.

## 5. Gesture feasibility

Legend: **now** = works in the current firmware; **small change** = firmware-only, hours, no new driver; **new driver** = needs a peripheral driver; **not possible** = not supportable on this hardware/with our constraints. Reliability = expected correct-recognition rate in normal use; FP = false-positive risk of an unintended trigger. Estimates are mine **[estimate]** unless otherwise stated.

| Gesture | Feasibility | Reliability | FP risk | Notes / suggested use |
|---|---|---|---|---|
| **Tap** | now (`G_TAP`) | High; slop 18 px | Low (but +330 ms where deferred) | Select a session / focus the ring segment. Use for non-destructive things; remove the deferral where no double-tap exists on that screen. |
| **Double-tap** | now (`G_DOUBLE_TAP`, 300 ms / 40 px) | Medium-high; needs a finger that does not jitter > 18 px | Low-medium (two accidental touches) | **Focus terminal** (already so). Cost: any single tap on the same target waits 330 ms. |
| **Long-press** | now (`G_LONG_PRESS`, 600 ms, still finger) | High | Low-medium (resting thumb) | **Snooze** or secondary action; shows a ring-fill from 0-600 ms. Move-cancels it (`core/gesture.c:92`). |
| **Press-and-hold with progress ring** | now for Approve (1200 ms bar, release cancels) ; **small change** for a ring-shaped progress and for arbitrary targets | High, hold_ms is measured and daemon-validated (>= 1000 ms) | Very low | **Approve** (and Deny if wanted). Add haptic pulses as progress feedback; keep >= 1000 ms. A bus glitch counts as a release and cancels safely (fails closed). Make the progress ring a thin arc (cheap to draw). |
| **Swipe (4 directions)** | now (`G_SWIPE_*`, >= 60 px, <= 400 ms, on release) | Medium-high; diagonals resolve by dominant axis | Medium (accidental brush against clothing) | Swipe right = ack (today). Not suited to destructive actions. No live feedback during the swipe. |
| **Circular swipe along the edge (ring scrub)** | **small change** (drag-stream + angle recogniser in `gesture.c`/`touch_read_cb`) | Medium-high for 6-12 sectors (45-30 deg = 157-105 px of arc at r = 200); edge accuracy at the rim **[unverified]** | Low (needs a continuous arc >= ~15 deg from the rim zone) | **Select the session** (detent haptic per sector). Use the touch annulus r in [170, 225] px, angle hysteresis ~1/3 sector, coalesce to 1 sample / 10 ms. Poll rate ~100 Hz is plenty (3.6 deg/sample at 1 rev/s). |
| **Button A/B press** | now (`G_BTN_A/B` on release, ~12-25 ms) | Very high | Low (pocket presses possible on a lanyard) | A = context primary (focus), B = back / next session. Needs on-screen hints at the button edges. |
| **Button A/B hold** | now (`G_BTN_A/B_LONG` at 600 ms; B-long currently = home) | Very high | Low | Hold A = ack/snooze; hold B = deny. Consider a second threshold (e.g. 1200 ms) for approve to match the hold-bar. |
| **Button A/B double-press** | small change (copy the touch double-tap logic; adds ~300 ms latency to single press) | High | Low | Optional; prefer chord/hold instead. |
| **Button chord (A+B)** | small change (`btn[0].down && btn[1].down`; both already tracked) | High (vendor firmware uses A+B hold for "menu") | Very low | Deliberate "back to menu" / "cancel all" or global mute. Avoid conflict with the vendor meaning only if the vendor firmware is restored. |
| **PWR button (PM1)** | new driver code (PM1 BTN regs/IRQ) | n/a | **High: double-click = power off** | **Do not use.** |
| **Wrist flick (IMU)** | new driver (BMI270 init + config blob + feature enable; or software gyro threshold) | Medium on a wrist; **unknown on lanyard** [unverified] | Medium-high (walking, desk bumps) | Optional "dismiss/snooze". Hardware feature set includes flick-in/out and jiggle but is tuned for a worn watch (4.8.7). Never gate approve on it. |
| **Tilt** | new driver (accel readout only, polled; 30 Hz stream already specified in HAL, `hal.h:77,86`) | Medium-high for coarse tilt (e.g. +-30 deg) | Medium (picking the device up) | Could change session ("tilt to scroll the ring") but conflicts with simply holding the device. Prefer for easter eggs. |
| **Shake** | new driver (BMI270 jiggle feature or software accel magnitude) | High for hard shakes | Medium (carried on lanyard) | Already routed to glance in `app.c:177`. Suggested use: "snooze all". |
| **IMU double-knock (tap the case)** | new driver + new algorithm (BMI270 has **no tap engine**; needs software detection on accel at >= 400 Hz, high-pass + peak timing) | Low-medium; surface/mount dependent | Medium-high | Not recommended as a primary gesture; if wanted, use only to dismiss alerts on the desk. High ODR also costs power and I2C bandwidth. |
| **Palm cover (touch area)** | not possible with confidence (single-point reader exposes no area/pressure; a self-cap panel may return erratic coordinates under a palm) | Low | High (pocket, sleeve) | Cannot be recognised reliably. Alternative: "whole-screen touch for 1 s" via long-press anywhere, which is just a long-press. |
| **Raise to wake** | new driver (BMI270 wrist-wear wake-up and/or "focus" detection; PMG0 -> PM1 -> G12 IRQ path is hardware-ready; no firmware) | Medium on wrist; **unverified for a lanyard/desk device** | Medium (walking, arm swing) | Biggest battery/UX win: display off until lifted/attention. The vendor demo shows full-feature wake at 671 uA (L1) or accel-only any-motion at 38 uA (`docs/github/M5StopWatch-PowerDemo.md:24-27`). Fall back to "any-motion" wake + a software tilt check. |

### Recommended minimal gesture set (given the above)

1. **Select**: circular scrub along the rim (small change) or A/B buttons for prev/next; tap on a ring segment as fallback (now).
2. **Act, one gesture** on the selected session by state: tap/button A = focus (now); long-press (600 ms) = ack or snooze; **press-and-hold >= 1.2 s with progress ring = approve** (now; keep daemon floor of 1000 ms); Deny = hold B or tap on a dedicated target.
3. **Add** a chord (A+B) for cancel/home and drop the 330 ms deferral wherever double-tap is not used.
4. **Defer** IMU gestures until a BMI270 driver exists; start with raise-to-wake via any-motion, treat shake/flick as optional extras and never as the sole confirmation for approvals.

## 6. Open questions and suggested measurements

1. Measure achieved fps (read `fps` from `dbg_ui_state`) during: ring colour change, a segment pulse, a full-ring rotate. Also log `heap_caps_get_free_size` for internal/DMA/PSRAM.
2. Log raw touch points at the rim (angle sweep at r = 170/200/225 px) to confirm edge accuracy and jitter; record the CST820 gesture byte b[1] for swipes/clicks to see what the chip reports (read-only, no config writes needed).
3. Check whether failed touch reads occur during haptic PWM writes (I2C contention) by counting `rd()` failures.
4. Confirm the PM1 LED is a single colour and whether charge state drives it in hardware.
5. Confirm which BMI270 INT pin is wired to PMG0 and whether PM1 can forward it to G12 as an IRQ.
6. Check whether the BMI270 config-blob load is required for plain accel/gyro readout.

## 7. Sources

- Vendor product/pin docs (local copies): `docs/StopWatch.md` (https://docs.m5stack.com/en/core/StopWatch), `docs/guide/usage.md`, `docs/github/M5StopWatch-PowerDemo.md` (https://github.com/m5stack/M5StopWatch-PowerDemo), `docs/github/M5StopWatch-UserDemo.md`, `docs/arduino/api/imu.md`.
- Datasheets (local): `docs/datasheets/CST820B_touch.pdf`, `CO5300_display_driver.pdf`, `M5PM1_power_management.pdf`, `M5IOE1_io_expander.pdf`, `ES8311_audio_codec.pdf`; schematic `docs/hardware/StopWatch_schematic.pdf` (only the text layer was grepped; net labels).
- BMI270 datasheet (online): https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf (rev 1.6, BST-BMI270-DS000-08).
- Libraries read: `firmware/managed_components/m5stack__M5PM1`, `m5stack__M5IOE1`, `lvgl__lvgl` (9.5.0), `m5stack__m5gfx`. No BMI270 or touch driver component is present in `managed_components`.
