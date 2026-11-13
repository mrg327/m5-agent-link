# Motion: raise to wake, face-down silence, orientation, palm cover, confirmation tones

Issue 20261112-fe29. Code: `components/core/src/motion.c` (the logic, pure, tested with scripted poses),
`prefs.c` (stored volume and orientation), `boards/stopwatch/imu.cpp` (BMI270 driver). Everything here
follows UI-V2's rule for secondary inputs: **motion only wakes, sleeps, silences or turns the picture.** It
never approves, denies, stops, sends, selects or dismisses anything, and a request waits unanswered through any
amount of motion (`sim_motion_raise_wake` checks that no command goes out).

## Sensor and driver

- BMI270 at I2C 0x68 on the system bus (SDA 47, SCL 48, shared with touch, RTC, codec and PM1). Chip ID 0x24.
- The interrupt is **not on an ESP32 pin**: BMI270 INT goes to PM1 GPIO0 (shared with the RTC), and PM1 GPIO1
  drives G12 (`docs/StopWatch.md`, M5PM1 table). Using it would mean configuring PM1's IRQ routing and
  decoding which source fired; the sensor is therefore **polled**: one 6-byte read every 100 ms from a small
  task (3 KB stack, priority 2). Nothing runs in the main loop; samples reach core through the existing
  `hal_motion_set_callback` ring.
- Bring-up (`imu.cpp`): soft reset, 8 KB Bosch config file uploaded in 32-byte bursts (BSD-3-Clause, kept
  unchanged in `bmi270_config.c`, const in flash, nothing held in RAM), `INTERNAL_STATUS` = init_ok, then
  accelerometer only: low-power filter, average of 4, 25 Hz, +-4 g, advanced power save on. The gyroscope and
  the feature engine stay off. The config file is not needed for plain accelerometer data; it is loaded so
  that a later step (any-motion on PM1's wake line, wrist-wear) needs no new bring-up.
- Axes: HAL convention is x right, y down the screen, z out of the glass, in milli-g, "accelerometer reading":
  face up and still reads z = +1000, held upright (12 o'clock up) y = -1000. The chip is mounted rotated, so
  the board swaps its X and Y (as the factory firmware does). Only z has been checked on a bench; **the signs of
  x and y are not yet checked against a moved watch** (see Unverified).
- Power: the datasheet puts accelerometer-only low-power mode at 25 Hz in the region of 14 uA. The ESP32 side
  costs a 6-byte I2C read per 100 ms (about 0.7 ms of bus time at 100 kHz, 0.7 %). A driver failure (50
  reads in a row) stops the stream and the watch behaves as before.

## Raise to wake (automatic)

A raise is: the screen was turned away from the viewer (z below 250 mg for 300 ms: hanging, pocket, face down),
then within 1.5 s of a jolt (a sample-to-sample change over 200 mg) it faces up (z at or above 550 mg), and
stays so for 200 ms (z not below 450). Then the watch is disarmed until it turns away again, and a raise cannot
repeat within 1.5 s. A bump on a desk, a slow tilt, or a swing shorter than 200 ms are not raises. A scripted
or hardware `lift` event counts as a raise too.

A raise does what a touch does to the power state and nothing more: a dark display lights, a glow is revealed
as the request card, the ambient dim ends and the nag stops (you looked). Not while face down.

## Face down means silence (automatic)

Face down is z at or below -800 mg while still (no change over 120 mg between samples) for 1.5 s. It ends 0.3 s
after z rises above -500 mg. While it holds:

- `feedback.c`: no haptic and no sound, including reminders and the finish buzz (the boot buzz is
  unconditional; stopping a pulse in progress is still allowed).
- No glow for an arriving request, and the display goes off after 3 s without input.
- The user's alert mode and quiet settings are never written; when the flag clears, the alert mode that was set
  applies again at once. The request itself and the ring still change.

## Orientation

Three stored values (`prefs.c`, key `orient`): `normal` (default), `flipped`, `auto`.

- `flipped` / `auto` turn the UI by 180 degrees. LVGL renders upright; the board's flush turns each band's
  pixels and area (`flush_rotate` in `display.cpp`, same in the simulator). LVGL turns pointer coordinates for
  widgets; `app.c` turns the raw point once more for the gesture recognizer, the bezel dial and the keyboard,
  so those work on what the user sees. The simulator's pointer is a raw panel point like the touch controller's.
- `auto`: 6 o'clock up (y at or above +650 mg, with |z| at most 800) and still for 1.5 s turns it; y at or
  below -350 mg for 1.5 s turns it back; between those, or lying flat, nothing changes. Any movement restarts
  the 1.5 s. The turn is deferred while a finger is down, a button is held, the keyboard is open or the voice
  recording is on, so it can never happen in the middle of a hold.
- **Default is `normal`.** Automatic turns the UI upside down if the sign of y is wrong, and there is no
  settings row yet to turn it off again; it should be enabled only after the sign is checked on a wrist
  (hold the watch upright and read `y` in the boot log line or a debug build). Build with
  `-DPREFS_ORIENT_DEFAULT=ORIENT_AUTO` to make it the default, or set `orient` through the HAL store.

## Palm cover

The CST820B datasheet has no contact-size or palm field; the controller reports positions (one point plus
a documented "real two-point" capability). The nearest safe signal is **two or more simultaneous contacts**
(`hal_touch_covered`, register 0x02 finger count). On that edge `app.c` stops the motor and speaker
(`feedback_silence_now`) and records the request currently on top as muted, so its 15 s reminders are
dropped; the next request alerts normally. It answers, dismisses and selects nothing. Unverified on hardware:
whether a palm reports two points, one point or none. If it reports one, the cover does nothing (a one-finger
touch behaves as always: it wakes, and touching ends the nag).

## Confirmation tones and volume

Two roles in the sound bank, each with two candidates for the Atlas to audition: `approved` (rising, 270 ms,
C5 G5 C6, `approved-rise`) and `denied` (falling, 340 ms, triangle D5 G4 C4, `denied-fall`). They are rendered by
the same synth on first use and cached in PSRAM (about 4.3 KB and 5.4 KB). They play from `feedback_approved`
and `feedback_denied`, i.e. when a hold completes and the answer went out (approve, rerun, dictate send, keyboard send,
deny, stop), only in alert mode "Sounds: on", not face down. Grant-option replies (`feedback_reply_sent`) keep their
three ticks and have no tone.

A third confirmation role, `updated` (`updated-flourish`, about 560 ms: FM C5 E5 G5 C6 at 80 ms each, then a bell E6 ringing
out for 240 ms), tells you the watch flashed. `ota.c` plays it from `feedback_updated` in `ota_on_link`, at the
first welcome of an unconfirmed (just-OTA'd) image, together with the toast "Updated to <fw>" (the toast shows even when
muted). A power-on or a USB flash boots an already confirmed image and a later re-welcome finds it cleared, so neither
plays it. Same gates as the other tones (Sounds on, volume above 0, not face down). Simulator: `sim_ota_updated_tone`
(`AGENTLINK_SIM_UNCONFIRMED=1`) and `sim_ota_updated_tone_silent`.

Volume (`prefs.c`, key `volume`): steps 0..5 map to 0, 10, 25, 45, 70, 100 %; default 5, which is the level the speaker
always had. `hal_audio_set_volume` converts to the ES8311 DAC attenuation (0.5 dB steps, 100 % = 0 dB, each
halving -6 dB) and the speaker task applies it before each sound; step 0 plays nothing. Stored in NVS.

## Settings rows (not built: STATUS belongs to another branch)

- Volume: a row stepping 0..5 with a short `approved` preview on change; calls `prefs_set_volume`.
- Orientation: a row cycling Auto / Normal / Flipped; calls `prefs_set_orient`.
- Both could be sent by the daemon in `cfg` later; no protocol change was made.

## Checks

`ctest`: `motion_logic` (unit, poses), `sim_motion_raise_wake`, `sim_motion_face_down`,
`sim_motion_face_up_restores`, `sim_motion_orientation` (dwell, hysteresis, no turn during a touch, raw swipe
through the turned UI, framebuffer equals the 180-degree turn of the upright one), `sim_motion_orientation_stored`,
`sim_motion_cover_mute` and `_control`, `sim_tone_approved`, `sim_tone_denied`, `sim_tone_quiet`, `sim_volume_step`,
`sim_volume_zero`. Simulator script commands: `pose NAME|X Y Z`, `cover`, `uncover`, `pref orient|volume V`,
`fb_mark`, `expect_fb_flipped`, `expect_flipped 0|1`.

## Unverified (needs a wrist)

- The x/y signs of the sensor mapping, therefore automatic orientation and the exact raise pose.
- Raise thresholds on a moving wrist and a lanyard: false wakes while walking (target in FIRMWARE.md S5: under 1 per
  10 min), and whether a slow raise (no 200 mg jolt) is missed.
- Whether the CST820B reports a palm as two contacts.
- Current draw of the sensor and of the 10 Hz poll with the display off (only datasheet and vendor figures exist).
- Hardware rotation of the picture was checked only through the screenshot read-back, not by eye.
