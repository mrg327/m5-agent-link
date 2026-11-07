# Wearable glance UI & gesture vocabularies — research for "one screen, select then act"

Scope: round-display wearables, gestures, haptics, colour, LVGL feasibility; then 4 designs for the StopWatch (466×466 round AMOLED, CST820 touch, 2 buttons, vibration motor, IMU).
Legend: ✅ = verified from the cited URL this session; ⚠️ = unverified / from general knowledge (re-check before relying on it).

## 1. Key findings by platform

### Apple watchOS
- ✅ **Double Tap = "primary action" of the current scene.** Available on Series 9/Ultra 2; the system also uses it to open the Smart Stack, scroll widgets, snooze, and "perform the primary action from notifications". https://www.apple.com/lv/newsroom/2023/10/apple-watch-double-tap-gesture-now-available-with-watchos-10-1/
- ✅ **Only one primary action per scene** (`.handGestureShortcut(.primaryAction)`); it can be attached to widgets and Live Activities in the Smart Stack; priority is primary action > scroll view. The system highlights the control that fired. https://developer.apple.com/documentation/watchos-apps/enabling-double-tap
  - *Lesson:* the ecosystem's answer to "too many submenus" is **one default action per focused item, with visible feedback of which control fired**. Copy this: the selected session has exactly one state-dependent default action.
- ✅ Smart Stack / Live Activities on watch (remote from iPhone) can host that primary action (same source as above). Short-look vs long-look notifications, Digital Crown select, Always-On dimming: ⚠️ not re-fetched; known pattern = short look (glance, auto-dismisses on wrist lower) → long look (detail + action buttons). Reference: https://developer.apple.com/design/human-interface-guidelines/notifications (⚠️ not fetched).
- ✅ **Haptic types** (WKHapticType): notification, directionUp, directionDown, success, failure, retry, start, stop, click, plus navigation left/right turn. Each is semantically named, i.e. the vocabulary is *meaning-based*, not pattern-based. https://developer.apple.com/documentation/watchkit/wkhaptictype
- ⚠️ Apple's HIG haptics page (https://developer.apple.com/design/human-interface-guidelines/playing-haptics) failed to render via fetcher; guidance there is "use system haptics sparingly, consistently, with their standard meanings" (⚠️ from memory).

### Wear OS
- ✅ Material 3 Expressive for Wear OS: **edge-hugging button** (full-width control at the bottom curve), `TransformingLazyColumn` (items scale/morph near the round edges), 3-slot tile `PrimaryLayout`, spring animations, dynamic colour. https://android-developers.googleblog.com/2025/08/introducing-material-3-expressive-for-wear-os.html
- ✅ Rotary input scroll is **no longer a mandatory** quality requirement (removed Feb 2024) — touch-only must work. https://developer.android.com/develop/adaptive-apps/quality-guidelines/wear-app-quality
- ⚠️ Tiles = swipe-in glance surfaces with 1 primary + up to ~2 chips; Ongoing Activities = persistent chip on watch face/launcher with a status string & icon. (Not fetched; see the Wear OS design docs under https://developer.android.com/design/ui/wear.)
- ✅ Android haptic principles: "less is more", clear > rich > buzzy ("if choice is buzzy or none, choose none"), scale strength with importance/frequency, use platform constants/primitives for consistency, provide fallbacks. https://developer.android.com/develop/ui/views/haptics/haptics-principles

### Samsung Galaxy Watch
- ✅ **Universal Gestures** (accessibility): Make Fist, Make Fist Twice, Pinch, Double Pinch; Pinch/Double-pinch = next/previous item, fist = select. This is a *cursor-walks-items, one gesture selects* model — the same "select then act" idea. https://www.samsungmobilepress.com/articles/more-than-a-gesture-how-galaxy-watchs-universal-gestures-feature-enhances-accessibility
- ✅ Physical **rotating bezel** on classic models; models without it have a **Touch Bezel** (swipe along the outer edge to move through items / adjust values). Touch & hold >2 s opens options. https://www.samsung.com/ca/support/mobile-devices/troubleshoot-touchscreen-issues-or-use-touch-gestures-galaxy-watch/
  - *Direct precedent for "swipe around the ring to move selection".*
- ⚠️ "Knock-knock"/wrist-gesture features — not confirmed in the fetched pages.

### Pebble
- ✅ Button model: Up / Select / Down on one side, Back on the other; Select opens launcher from watchface, Down = Timeline Future, Up = Timeline Past; Timeline Quick View shows next pin on the watchface, Back dismisses, Down opens it, Select = more info. https://help.repebble.com/en/articles/14489701-pebbleos-overview and https://help.repebble.com/en/articles/14489872-timeline-calendars
  - *Lesson:* a hardware "Back = dismiss/ack" and "Select = act" with no menu depth. The StopWatch's 2 buttons can be **Select/Act** and **Back/Dismiss**.

### Garmin
- ✅ Glances: a widget's one-line-ish preview in a vertical list; rendered with tiny memory (≈32 KB on most devices), content cached; a single view only, no paging in glance mode. Tap/Select opens the full widget. https://developer.garmin.com/connect-iq/articles/core-topics/Glances.html
  - *Lesson:* glance = cached, ultra-cheap rendering; detail only on demand.

### Fitbit/Pixel Watch, Oura, Whoop
- ⚠️ Not verified this session (search rate-limited). General knowledge: Pixel Watch follows Wear OS (above); Whoop/Oura have no/minimal screens — Whoop uses haptic-only alarms, Oura uses LED/haptic nudges. Takeaway if true: **haptic pattern count must stay tiny (≈3–5)**; treat as design heuristic, not citation.

### Radial / ring / orbit UIs
- ⚠️ No formal source fetched. Established patterns: radial (pie) menus — fast selection by direction (Fitts' law advantage, direction memory); arc progress rings (Apple Activity rings; Wear OS `CircularProgressIndicator`); clock-position layouts as on watch complications. Samsung's Touch Bezel (above) shows ring-edge swipe is accepted on shipping products.
- Ring capacity rule of thumb (⚠️ my estimate, not sourced): at 466 px with ~44 px min touch target at radius ~200 px, circumference ≈1257 px → ≈ 28 targets max, practically **≤12 slots at ≥ 30° each** is comfortable.

### IMU gestures
- ✅ Research on wrist-gesture recognition exists (e.g., OpenWatch benchmark: 10 h IMU+PPG, 50 participants, 59 gestures; https://arxiv.deeppaper.ai/papers/2605.04791v2) but I **found no published per-hour false-positive rate for simple flick/shake/tilt on commodity watches** (⚠️ gap).
- Apple's own approach to the problem: Double Tap uses S9 + PPG/accelerometer/gyro ML (⚠️ not in fetched text) and is limited to "primary action", i.e. low-consequence by design. Samsung ships pinch/fist only in an *opt-in accessibility* mode (✅ above) — a hint that ambient IMU gestures are not trusted as default input.
- Design guidance (⚠️ engineering judgement): use IMU only for **low-consequence, reversible actions** (move selection, wake, scroll); require **arm-orientation gating** (screen facing up/wrist raised), **debounce/refractory 300–500 ms**, a **two-phase gesture** (e.g., tilt past threshold *and* return) and **a visible arming state**. Never use IMU alone for approve/deny. Measure false triggers on the StopWatch (log 24 h of candidate events from desk + walking) before shipping.

### Haptics: making patterns distinguishable
- ✅ Apple semantics (success / failure / retry / start / stop / directionUp / directionDown / click / notification) — https://developer.apple.com/documentation/watchkit/wkhaptictype
- ✅ Android: strength ∝ importance, subtlety for frequent events, ramping amplitude to signal approaching a target, avoid buzzy long vibrations, provide fallback for rich haptics. https://developer.android.com/develop/ui/views/haptics/haptics-principles
- Recommended vocabulary for an ERM/LRA motor (⚠️ design proposal; verify motor bandwidth — a basic ERM cannot do crisp short ticks): distinguish by **count and rhythm, not intensity** — 1 short = tick/selection; 2 short = done; 3 short = needs input; 1 long = error; rising ramp (short-short-long) = permission pending; falling = deny/cancel; hold-to-confirm = continuous low buzz that ends with 1 firm click. Keep ≥150 ms gaps; max 3 pulses.

### Colour-as-state, burn-in, power
- ✅ Colour-blind-safe base palette: Okabe-Ito — orange #E69F00, sky blue #56B4E9, bluish green #009E73, yellow #F0E442, blue #0072B2, vermillion #D55E00, reddish purple #CC79A7, black #000000; empirically (not guaranteed) CVD-safe. https://vizcept.com/blog/okabe-ito-palette-guide
- Proposed mapping on black (⚠️ proposal): working = blue #0072B2→sky #56B4E9 (calm, animated); done = bluish green #009E73; needs input/permission = orange #E69F00 (pulsing); error = vermillion #D55E00; info/idle = grey #999. **Never rely on colour alone**: pair with shape/animation (solid / sweeping / pulsing / broken ring) so red-green CVD and glare still work.
- Apple/Google colour usage: Apple Activity rings and Wear OS dynamic colour (✅ dynamic colour mentioned in M3 Expressive post above) — specific ring-colour semantics not verified.
- AMOLED burn-in/power (⚠️ not sourced this session; standard practice): black = pixels off → near-zero power; keep always-on view mostly black, thin outlines, dim, **pixel-shift every ~1 min**, avoid static bright large fills, limit lit pixels to <~15 % in ambient. For the StopWatch: after N s idle drop to a ring-only dim "ambient" (thin 2–3 px arcs, 20–30 % brightness) and shift 1–2 px per minute.

### LVGL 9 feasibility (brief)
- ✅ `lv_arc`: background arc + indicator arc, `lv_arc_set_bg_angles`, `lv_arc_set_range/value`, `lv_arc_set_rotation`, modes NORMAL/REVERSE/SYMMETRICAL; 0° = 3 o'clock, clockwise. https://lvgl.io/docs/open/widgets/arc
- ⚠️ Not fetched: `lv_scale` (round-inner/outer ticks, good for 12-slot clock tick marks), `lv_anim` (property animation, path callbacks, can drive arc angle/opacity), `lv_obj_set_style_arc_color`, rounded arc ends. Plan: **one lv_arc per session** (N arcs of equal sweep minus gaps), each with its own colour/opacity; pulse via `lv_anim` on arc width/opacity. Cost concern at 466×466 on ESP32-S3: full-screen arc redraws are the expensive part — use partial-refresh, a PSRAM draw buffer, and invalidate only changed arc areas. Verify FPS on-device; see `docs/research/ui-animation.md` in this repo for existing notes.

## 2. Cross-cutting principles
1. **One default action per focused item** (Apple primary action) — derive it from state, show it as the centre label/colour.
2. **Selection moves cheaply, action is one gesture, destructive/irreversible needs hold** (Android: strength ∝ importance).
3. **Selection motion = ring** (Samsung Touch Bezel precedent); no pages.
4. **Always acknowledge with haptic + highlight flash** on the control that fired (Apple highlight behaviour).
5. Keep touch-only fully workable; IMU/buttons are accelerators (Wear OS dropped required rotary support).

## 3. Proposed designs ("one screen, select then act")

Common layout: black background; a thick ring of N arc segments (gap 4°) at radius ~190–230 px; each segment's colour = state; selected segment is thicker/brighter with a "comet" dot; the centre disc (≈260 px) shows the selected session's name, state, elapsed time, and the **default-action label** (e.g. "FOCUS", "ACK", "HOLD ✓").

### Design A — "Bezel Ring" (touch-first)
- **Gestures:** circular drag along the rim = move selection (Samsung Touch Bezel style); tap centre = default action; tap a segment = select+default; long-press centre (600 ms) = secondary (snooze/ack-only); permission: **press-and-hold centre 1 s (arc fills) = approve**, **swipe down from centre across the disc = deny**; button 1 = act, button 2 = back/dismiss all-done.
- **Haptics:** tick per segment crossed (very subtle); 2 short = ack'd; ramp = approve filling; click at completion; long = error on deny/failed send.
- **Colour/anim:** Okabe-Ito mapping above; working = slow clockwise sweep on its arc; needs-input = orange pulse 1 Hz; done = solid green then fades to dim after ack.
- **Scale:** 1 = single full 360° ring acts as progress ring; 5 = 72° segments, large targets; 12 = 30° segments (≈100 px arc at r=200) — marginal; fall back to selecting by rotation drag only, and cluster "done" ones into a dim merged segment.
- **Risks:** accidental drag-to-select while taking a tap (use 12 px slop); discoverability of rim drag (show a hint comet on first wake); hold-to-approve must show clearly what is approved (name + tool).

### Design B — "Clock Orbit" (item per clock position)
- Sessions sit as dots (r≈22 px) at fixed clock positions (1–12), ordered by urgency starting at 12 o'clock; dot size = recency, colour = state, halo = needs attention.
- **Gestures:** tap dot = select + default action (needs-input→focus; done→ack+focus); double tap not needed; long-press dot = secondary menu as a **mini radial of 3–4 icons around the dot** (focus / snooze / ack / deny) with release-on-icon (one continuous press-drag-release gesture); permission: hold dot 1 s = approve.
- **Haptics:** 1 short on select, pattern by state on action as in §1.
- **Scale:** 1 = centred big dot; 5 = positions 12,2,5,7,10 (spaced); 12 = full clock face (dots 40 px, still tappable). Best direct mapping to number-of-sessions; stable positions → muscle memory ("session in terminal tab 3 is always at 3 o'clock" if mapped to tab order).
- **Risks:** position instability when sessions come and go (keep slot pinned per session); adjacent dots at 12 sessions → mis-taps; radial release-drag is an advanced gesture needing a first-use tutorial.

### Design C — "Tilt-Scrub + Tap" (IMU accelerator over Design A)
- **Gestures:** hold the button 1 (arming) and tilt wrist left/right to scrub selection (like a dial); release = stay selected; tap = act; shake is *not* used. Without the button held, IMU does nothing except wake on wrist raise.
- **Haptics:** tick per step; stronger detent at ends.
- **Scale:** 1 – n/a (no scrub); 5 – 15–20° tilt per step; 12 – hysteresis/velocity-based scrubbing with acceleration; works one-handed.
- **Risks:** IMU false positives are the main hazard (no published rates found, §1) — the button-as-clutch removes them; fatigue; tilt direction depends on wrist side (config). Never IMU-triggered approve.

### Design D — "Two-button Crown Emulation" (hardware-first, glance-safe)
- **Gestures:** button 1 short = next session (clockwise), button 1 long = previous; button 2 short = default action, button 2 long (hold, arc fills) = approve/secondary; touch is optional (tap = select+act). Permission deny = button 2 double-press.
- **Haptics:** same vocabulary; button click gives native feedback.
- **Scale:** 1 = button 2 only; 5 = ≤4 presses; 12 = long cycling — add "jump to next attention" on button 1 double-press (skips to the next needs-input/permission segment, which is what users actually want).
- **Risks:** pocket/bump presses (require short-lived "awake" state; unawake press only wakes); discoverability low without on-screen labels — draw button hints at the screen edge near each button.

### Recommendation
Build **A as the base** (touch ring, one-gesture default action) with **D's buttons** as eyes-free accelerators and **"jump to next attention"**; adopt **B's pinned clock slots** if ≥6 sessions are common; treat **C** as optional/experimental behind a setting after measuring false triggers. Common safety rule: **approve = hold-to-confirm only; deny = distinct directional gesture; nothing destructive from IMU**.

## 4. Gaps / follow-ups
- Not fetched: Apple HIG notifications/Always-On/complications pages, Wear OS Tiles/Ongoing Activity specifics, Samsung knock gesture, Fitbit/Pixel, Oura/Whoop, burn-in sources, LVGL scale/anim pages, quantitative IMU false-positive rates.
- Needs on-device test: motor type/bandwidth for distinct patterns, LVGL ring redraw FPS at 466×466, touch slop for CST820 rim-drag, CVD simulation of the final palette on the actual AMOLED.
