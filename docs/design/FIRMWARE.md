# Firmware architecture (draft)

> Status: **[P] draft for discussion.** The target is the StopWatch first (DESIGN.md R10). The code is designed so a second board needs only a new BSP.
> Hardware facts come from `docs/StopWatch.md` (pin map) and `docs/arduino/api/m5pm1_m5ioe1.md`.

## 1. Stack
- **ESP-IDF 5.5**, **LVGL 9.5**, **NimBLE** (BLE peripheral only; Wi-Fi and Classic BT are compiled out), and **zcbor** (generated from `protocol/agentlink.cddl`).
- The StopWatch BSP uses the M5GFX `Panel_CO5300` (QSPI 80 MHz) as the panel driver, with LVGL flush glue. This follows the factory firmware approach (`docs/research/ui-animation.md`).
- M5PM1 / M5IOE1 drivers are taken from the M5Stack libraries, either ported or wrapped as IDF components.

## 2. Source layout
```
firmware/
  CMakeLists.txt, sdkconfig.defaults, sdkconfig.<board>
  components/
    agentlink_proto/   generated zcbor C + envelope, COBS, fragmenter, AEAD (mbedTLS/libsodium-lite)
    core/              portable: app model, controllers, UI (LVGL), power policy, haptic/sound patterns
      model/           session store (fixed pool, ~32 sessions), attention queue, settings (NVS)
      link/            link manager: transport-agnostic, pairing, session keys, resync
      ui/              screens: ring (single screen, UI-RING.md), badge, settings, pairing; layout profiles
      fx/              creature sprites + animation engine, easter eggs (IMU)
      power/           power state machine (policy only; calls HAL)
    hal/               C interface headers only (display, touch, buttons, haptic, audio, imu, rtc, power, batt, link_ble, link_usb)
  boards/
    stopwatch/         BSP implementing hal/ for CO5300, CST820B, BMI270, RX8130CE, ES8311, M5PM1, M5IOE1
  main/                app_main: board_init() → core_start()
simulator/             same core/ + proto/ on desktop: SDL display, mouse = touch, keys = buttons, WS link
```
The rule is that `core/` never includes a board header; it only talks to `hal/`. A second board adds `boards/<name>/` and a layout profile.

## 3. Tasks (FreeRTOS)
| Task | Core | Prio | Role |
|---|---|---|---|
| `ui` | 1 | mid | `lv_timer_handler`. It is the **only** task that touches LVGL and consumes the `ui_evt` queue |
| `link` | 0 | high | NimBLE host + USB CDC RX/TX, framing/crypto, decodes messages → `model_evt` queue |
| `app` | 0 | mid | Owns the model. It applies `model_evt`, produces `ui_evt` + haptic/sound, and turns input events into `cmd`s |
| `input` | 0 | mid | Touch (CST820B) and buttons (G1/G2) → gesture recognizer → `input_evt`; IMU interrupts (lift, shake, tilt) |
| `audio` | 1 | high | (v3) Mic capture + ADPCM encode → link; chimes via ES8311 |
| `power` | 0 | low | Idle timers, decides power state transitions, battery sampling |

Communication between tasks uses queues only, with no shared mutable model across tasks except read-only snapshots for the UI.

## 4. Power states [P]

Measured rails (from vendor docs): **L0** = M5PM1 only. **L1** = + IMU + RTC. **L2** = + ESP32-S3 (sleeping). **L3A** = ESP32 active. **L3B** = AMOLED supply (PYG8). Audio power is PYG3, and the speaker amp is PYG10.

```
            lift/btn/attention                     no input 8 s
 STANDBY ───────────────────────► ACTIVE ─────────────────────► STANDBY
 (BLE connected,                 (display on,                   
  display rail off,               30 fps UI,
  light sleep)                    BLE fast interval)
    │  BLE lost > 10 min / batt < 10% / user "deep sleep"
    ▼
 DEEP (L1: only IMU+RTC+PM1; BLE off)  ── lift / button / RTC timer (adaptive backoff) ──► boot → reconnect
 BADGE: like ACTIVE, display always on (dimmed after 60 s), animations at 30 fps
```
- **STANDBY** is the default "multi-day" state (Q4). Its properties:
  - ESP32 in auto light-sleep, with NimBLE kept connected (supported in IDF with modem sleep).
  - The connection interval is negotiated to about 1 s with slave latency, so the radio wakes rarely.
  - AMOLED rail (L3B) **off**, touch controller in sleep, audio rail off.
  - Wake sources: the BLE event, the button GPIOs, and **G12** (M5PM1 IRQ chained from the IMU INT1 on PMG0).
- **Alert at rest:** a BLE `attention` arrives during STANDBY. The device vibrates (PYG9 PWM). If the user is in quiet mode the speaker stays off; otherwise it plays the chime. It then turns on L3B and shows the **dim ring glow** for about 2 s, then turns L3B off again.
- **Lift detection on a lanyard:** plain any-motion would fire constantly while walking. Instead we use BMI270 features:
  - **orientation/flat change** plus a **"tilt toward face"** heuristic. When hanging, gravity runs along the screen's −Y axis. When lifted to read, the screen normal points roughly up or toward the user.
  - It must be held for ≥ 300 ms.
  - The BMI270 "wrist wear wake-up" feature may be repurposed for this, which needs a spike.
  - The same IMU data picks a 0° or 180° rotation.
- **DEEP [D]:** when the host is lost, the device keeps advertising and reconnecting with **adaptive backoff** (5 → 10 → 20 → 40 → 60 min windows, via RTC timer wake). After about 4 h without success it stays in DEEP until the user **lifts the device or presses a button**, which triggers a manual reconnect.
- **Touch-to-wake [D]:** off in STANDBY. Only lifting or a button wakes the device.
- **As built (2026-11-08):** `core/power.c` owns the policy and `boards/stopwatch/board.cpp` the mechanism. Display off after 60 s without input. DEEP = ESP32-S3 deep sleep (PM1, IOE1 and the RTC domain stay up), entered only on battery with no link, 10 min after display-off; never with USB present (checked twice: `may_deep()` and the board guard). Wake: either button (G1/G2 via EXT1, any low) or the retry timer (5, 10, 20, 40, 60, 60, 60 min, the count kept in RTC memory, then buttons only); a wake is a reboot. A timer wake boots dark and silent, tries to reconnect for 90 s, then sleeps again; a link or any input resets the ladder. A held button aborts the sleep. Recovery: one click of the PM1 power button resets. Lift-to-wake waits for the IMU driver. Simulator: `--battery`, `--deep-after S`, `--wake timer|button --tries N`; tests `sim_deep_*`.

- **Measured (proto 0.19):** the watch reports its battery voltages and the time in each state (display on, dimmed, off, deep) every 5 minutes while linked; `agentlink power` fits a drain rate per state ([POWER.md](POWER.md)).

**Budget estimate (to be measured in spike S4):**

| State | Est. avg current | Notes |
|---|---|---|
| STANDBY (BLE 1 s interval, light sleep, display off) | ~1.5–3 mA | ESP32-S3 light sleep + BLE; PM1, IMU, and RTC are in the µA range |
| ACTIVE (display on, mid brightness) | ~60–120 mA | AMOLED cost depends on the content (black = cheap) |
| BADGE | ~50–100 mA | Mostly black scenes |
| DEEP (L1) | < 0.1 mA | |

With 450 mAh, STANDBY at 2.5 mA is about 7 days, and 40 lifts × 10 s/day at 100 mA adds about 11 mAh/day. **3 working days is plausible** if STANDBY stays under ~5 mA. Badge mode costs about 4–8 h of display-on time.

## 5. Display pipeline (StopWatch BSP)
- 466×466 RGB565. LVGL partial render with two 466×48 buffers in internal DMA RAM (~90 KB).
- A rounder callback aligns areas to even x/y (CO5300).
- Flush is DMA via the M5GFX panel. It is optionally gated on TE (G38) when the dirty area is larger than 25% of the screen.
- 30 fps cap for the UI, `LV_DEF_REFR_PERIOD 33`. The only full-screen animations are screen transitions (~250 ms) and the badge.
- Brightness uses CO5300 `0x51`. The display sleeps with `0x28` + `0x10` before cutting L3B.
- Burn-in: no static bright elements in ambient/badge; a ±2 px orbit shift every 60 s.

## 6. Input & gestures
- Touch: CST820B on I2C (G47/G48) with INT on G13. The raw points are fed to LVGL, which polls every 10 ms on the LVGL thread, so the drag stream is already coalesced to the latest point. The recogniser in `core/gesture.c` handles the following (see [UI-RING.md](UI-RING.md)):
  - **tap** (immediate; the 330 ms deferral and double-tap-to-focus are gone)
  - long-press (≥ 600 ms); the approve hold needs ≥ 1.2 s on the centre, with an arc fill
  - swipe (≥ 60 px, ≤ 400 ms); swipe-down on the centre = deny
  - **rim scrub**: starts in r 160–260 px around (233,233) (the glass edge is r 233, where a finger riding the bezel lands), tracks within r 110–340, ≥ 10° continuous arc to start, sector hysteresis of 5° past the boundary (capped at 1/3 sector). Once scrubbing, tap/swipe are suppressed for that touch. Events: `G_SCRUB_START/SCRUB/SCRUB_END`.
- Buttons: KEYA (yellow, G2) and KEYB (blue, G1) with debounce. A short = default action, A double (≤ 300 ms) = next attention, B short/long = next/previous session, A+B chord = detected and logged only. A single press is therefore delayed by ~300 ms to wait for a possible double (B is immediate).
- Haptics (`feedback.c`): scrub detent = 15 ms kick (the board runs ≤ 15 ms pulses at kick duty), rate-limited to 45 ms; approve-hold ramp = 13 steps (~1 s); per-state entry patterns.
- UI is a single Ring screen (`ui_home.c` + `ui_ring.c`, per-segment invalidation). `ui_glance/list/detail.c` were removed.
- Touch-panel wake is optional (off in STANDBY to save power; lift or a button wakes the device).

## 7. Link manager
- BLE peripheral with the custom service (PROTOCOL.md §1). It advertises only when unpaired or disconnected, with a slow advertising interval (1–2 s) to save power. It bonds with LE-SC.
- USB CDC is always available when plugged in. It is used for pairing, logs, and a docked mode. With USB connected, the device is in "docked" power policy (no sleep limits).
- On (re)connect: `hello` → `welcome` → `snapshot`. The link manager handles the app-layer AEAD and replay counters.
- Multi-host is future work. The model already tags each session with `host`, and NimBLE supports several connections.

## 8. Storage (NVS)
- Bonds/keys (encrypted NVS), user settings (alert mode, quiet hours, brightness, privacy, handle for the badge), and the last snapshot (optional, for a "stale" view after reboot).

## 9. OTA (proto 0.15, issue b769)
- **Slots.** Two 6 MB app slots (`ota_0`, `ota_1`) on the 16 MB flash. A USB flash writes `ota_0` and
  resets `otadata`; an update writes the slot that is not running and then boots it.
- **Transfer.** `agentlink ota firmware/build/agentlink.bin` pushes the image over whatever link the watch
  is on (BLE or USB): `fw_begin`, 2 KB `fw_chunk`s with up to 4 in flight, `fw_ack` per chunk
  ([PROTOCOL.md](PROTOCOL.md) §6c). Core (`core/ota.c`) runs the transfer from the main loop, checks the
  SHA-256 and shows the Updating screen (B held cancels); the board (`boards/stopwatch/ota.cpp`) writes the
  slot with `esp_ota_*`, erasing sector by sector as the image arrives. A dropped link resumes from the last
  written byte if the daemon asks again within 30 s. Over BLE the watch asks for a 7.5 ms connection interval
  for the transfer (the interactive 15–30 ms if the central refuses) and its usual one afterwards. A central
  without data length extension moves only a few 27-byte link-layer packets per connection event, so the
  interval sets the rate: on the Linux laptop with the BCM4350C0 a 1.3 MB image took 12 min at the central's
  48.75 ms and takes about 100 s at 7.5 ms (issue 2047). BlueZ remembers the last interval a peripheral asked
  for and connects with it next time; a connect below 15 ms makes the watch ask for 30–45 ms again. The
  watch refuses an update while it records, or below 15% battery unless it is charging, and stays out of
  standby while one runs.
- **Delta updates** (proto 0.21, issue ef2c). The daemon may send a patch against the running image instead
  of the image. Before it answers, the watch reads the first `bsize` bytes of its running slot
  (`hal_ota_base_read`) and compares their SHA-256 with `base`; a mismatch is `bad` "Base mismatch" and the
  daemon sends the whole image. Otherwise core feeds each patch chunk to the vendored detools decoder
  (`core/third_party/detools`, sequential bsdiff + heatshrink, window 2^11 / lookahead 2^10 only), which reads
  the base and writes the rebuilt image through `hal_ota_write` in 4 KB pieces. The decoder (about 2.3 KB,
  mostly heatshrink's window) and the 4 KB buffer sit in PSRAM. After the last patch byte the image length
  must equal `size`; then the SHA-256, signature, rollback and resume work as for a whole image. A patch that
  does not decode is `bad` "Bad patch". Hashing the base blocks the main loop for well under a second on a
  1.3 MB image (`hash_ms` in the `ota_begin` event).
- **Signing.** Images are signed (ESP-IDF "Require signed app images" without hardware secure boot, RSA-3072
  Secure Boot V2 format; `sdkconfig.defaults`). Nothing is burned into eFuses: a USB flash still boots any
  image. An update is accepted only if it is signed by the key in the running image's signature block, so
  nobody in BLE range can flash the watch. The key is per machine, outside the repository:
  `$AGENTLINK_FW_KEY`, else `~/.agentlink/fw_signing_key.pem`, made by the first build
  (`firmware/CMakeLists.txt`). Keep it: a watch flashed with one machine's key only takes updates built with
  that key, until it is flashed over USB again. After changing the key path, delete `firmware/sdkconfig`.
- **Rollback** (`CONFIG_APP_ROLLBACK_ENABLE`). A new image boots unconfirmed. Core confirms it once a daemon
  welcomes it; if that does not happen within 5 minutes, or the image resets first (a panic, a watchdog), the
  bootloader boots the previous image again. The daemon reads the result from the `fw` in the next `hello`.
  That confirmation (and only that one) plays the `updated` tone and toasts "Updated to <fw>" (MOTION.md, confirmation tones).

## 10. Spikes to de-risk
| ID | Spike | Exit criteria |
|---|---|---|
| S1 | Copilot/Claude permission hook: can device + terminal answer in parallel? | A documented flow that works for both agents |
| S2 | Rust BLE central (`btleplug`) ↔ NimBLE peripheral on mac/Win/Linux, including bonding | Notify/write round trip < 100 ms on all 3 |
| S3 | CO5300 + LVGL 9.5 on IDF using the M5GFX panel | 30 fps for a list scroll; screen off/on works |
| S4 | Power: STANDBY with BLE connected, display rail off | Measured mA; ≥ 3 days projected |
| S5 | BMI270 lift-to-read on a lanyard | < 1 false wake per 10 min of walking, wake in < 500 ms |
| S6 | Focus drivers: kitty, Terminal.app, Windows Terminal | Double-tap focuses the correct tab |

## 11. Observability (DESIGN §0) [D]
- **Debug channel:** the USB CDC carries two logical streams, protocol frames (COBS) and a debug stream. The debug stream is JSON lines prefixed with `#`, so it never collides with COBS frames, because COBS output contains no `#`-led lines after the zero delimiters. Each debug line has the shape `{"ts_us":…,"cat":"ui|input|power|link|fx|haptic|perf","ev":…,…}`.
- **What is logged:**
  - **ui:** every screen change, selection change, and the `ui_state` snapshot on request
  - **input:** every recognized gesture with its coordinates and timing
  - **power:** every state transition and the reason for it
  - **haptic/sound:** each pattern played
  - **perf:** frame time p50/p95 every 5 s while active, plus free heap and PSRAM
- **Debug commands** (USB, or BLE when debug mode is enabled): `ui_state`, `screenshot` (the RGB565 framebuffer streamed in chunks and converted to PNG by the daemon), `inject_input {tap|swipe|button|lift}`, `set_log_level`. The simulator implements the same commands, so the agent tooling is identical for both.

## 12. StopWatch board gotchas
- **Link layout:** `boards/stopwatch/link.cpp` implements the `hal_link_*` dispatcher and fans out to `link_usb.cpp` and `link_ble.cpp`.
- **`sw_power` component:** the M5PM1/M5IOE1 vendor libraries change their class layout when `<M5GFX.h>` is includable. They therefore live in the separate `sw_power` component, which has no m5gfx dependency, plus an `#error` guard if M5GFX headers become visible. Do not move that code into the board component.
- **M5IOE1 pin numbering:** the library API is 0-based: label `IOn` = `M5IOE1_PIN_n` = `n-1`. `sw_power.cpp` checks the `SW_IOE_*` constants against the library enum with `static_assert`.
- **`WHOLE_ARCHIVE`:** the board component is registered with `WHOLE_ARCHIVE` because the `hal_*` implementations are split across objects and are only referenced by `core`; without it the linker drops them.
