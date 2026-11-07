# UI proposal: "Ring": select a session, act with one gesture

Status: **implemented** (branch `ui-ring`; simulator-verified, firmware builds, not yet validated on the device). It replaces the old glance → list → detail screens.

**Implementation notes and deviations**
- Code: `ui_home.c` (screen), `ui_ring.c` (segments, per-segment invalidation in ≤ 30° chunks), `gesture.c` (rim recogniser, buttons), `feedback.c`, `ui_theme.c` (Okabe-Ito).
- Drag stream: no extra HAL queue. LVGL polls touch every 10 ms on the single UI thread, so the stream is naturally the latest point; the simulator uses the same path.
- Ack: the protocol has an `ack` op (1), but the daemon stores nothing, so the device also dims the segment locally (`seen`).
- Long-press on Working/Needs input/Idle = Focus; on Done/Error = ack only; ignored on Permission (the hold is the approve).
- Tap on a Permission centre = Focus (never approve).
- A double-press costs ~300 ms on A's single press (B is immediate). Accepted trade-off.
- A+B chord: detected and logged (`chord` event) with no action.
- Edge hints sit just inside the ring beside the confirmed buttons: A (KEYA, orange) at lower-left 7:30 (225° clockwise from 12), B (KEYB, red) at lower-right 4:30 (135°). A small dot points at each button.
- Approve arc and haptic ramp start after 250 ms of hold; early release cancels.
- Debug events: `scrub` (`phase` start|move|end, `sector`, `angle`, `arc`), `select`, `chord`; `screen` is `ring` or `attention`.
- Permission nag: up to 3 re-buzzes every 15 s. Ambient dim after 30 s idle when nothing needs input.
- Moved the host recogniser test to `simulator/tests/test_gesture.c`.
- Still needs on-device validation: rim accuracy, fps, haptic feel, hint placement against the real bezel.
Sources: [`wearable-glance-gestures.md`](../research/wearable-glance-gestures.md),
[`minimal-peripherals.md`](../research/minimal-peripherals.md),
[`stopwatch-interaction-capabilities.md`](../research/stopwatch-interaction-capabilities.md).

## 1. Problem

User feedback on the current firmware is "too many submenus". Today, acting on a session means opening the list,
tapping into the detail screen and then using the action there, plus a 330 ms tap deferral. All three research tracks
reach the same conclusion: **one screen, cheap selection, one gesture to act, and the action is chosen by the
session's state.**

## 2. The screen

```
              ╭── segment per session (colour = state) ──╮
           ╭──────────────────────────────────────────────╮
          │   ◜ ▬▬▬ ▬▬▬ ▬▬▬                              │
          │          ┌───────────────┐                    │  ← [A] edge hint: "FOCUS"
          │          │  claude · api │                    │
          │          │   ● Needs you │                    │
          │          │     4m 12s    │                    │
          │          │   HOLD ✓      │  ← default action  │
          │          └───────────────┘                    │  ← [B] edge hint: "NEXT"
          │   ▬▬▬ ▬▬▬ ◟                    USB · laptop   │
           ╰──────────────────────────────────────────────╯
```

- **Outer ring.** A band about 14 px wide at r ≈ 200–214 px, with one arc segment per session and 4° gaps.
  - Each segment's colour, shape and motion encode its state (§4).
  - The selected segment is thicker and brighter and carries a small "comet" dot.
  - Segments are pinned by session, so they don't jump around when others come and go. They're ordered by
    creation, and new sessions are appended clockwise from 12 o'clock.
- **Centre disc** (about 260 px). It shows the selected session:
  - the agent and the cwd basename;
  - the state word and its icon;
  - elapsed time;
  - the **default-action label** (FOCUS / ACK / HOLD ✓).
- **Edge hints** next to the physical buttons (Pebble ActionBar style), tinted with the state colour.
- **No sessions.** The ring becomes a single dim idle circle, and the centre shows the host and link (`USB · laptop`).
- **One session.** The whole ring is that session's colour, and it doubles as the progress ring for holds.
- **Nine or more sessions.** "Done" sessions merge into one dim segment, and you can scrub into it to expand it.
  Ring scrub selection still works at any count.

## 3. Gestures

The rule: **moving the selection is cheap, acting is one gesture, and anything irreversible needs a hold.**

| Input | Effect |
|---|---|
| Circular drag around the rim (starts at r 160–260) | Move the selection, with a haptic detent per segment. *Small firmware change: drag stream plus angle recogniser.* |
| Tap a segment | Select it, no action |
| **Tap centre** | **Default action for the state** (table below). No deferral: double-tap is retired. |
| Long-press centre (600 ms, ring fills) | Secondary action |
| Hold centre ≥ 1.2 s (permission requests only) | **Approve**, with an escalating haptic as the arc fills and a click on commit. The daemon floor is 1000 ms. |
| Swipe down across the centre (permission requests) | **Deny** |
| **Button A** short | Default action (eyes-free version of tapping the centre) |
| Button A double | **Jump to next attention**: the next needs-input or permission request, in priority order |
| Button B short / long | Next / previous session |
| A + B chord | Snooze all / mute for 30 min |

### Per-state default actions

| State | Tap centre / A | Long-press | Notes |
|---|---|---|---|
| Working | **Focus** terminal | Snooze this session | Nothing destructive |
| Needs input | **Focus** terminal | Quick-reply "continue" (later) | |
| Permission | **Focus** (review first) | Hold ≥ 1.2 s = **Approve**; swipe down = Deny | Never approve on a tap |
| Done | **Focus**, and acknowledge it | Acknowledge only (dismiss) | Ack dims the segment |
| Error | **Focus** terminal | Dismiss | |
| Idle / snoozed | Focus | Un-snooze | |

Focus is the dominant default because returning to the agent is the product's core promise. "Done" acknowledges as
a side effect of focusing.

## 4. State language

The palette is Okabe-Ito, chosen to be colour-blind safe. Every state also has a distinct shape and motion, so colour
is never the only cue.

| State | Colour | Icon | Motion (ring segment) | Haptic on entry |
|---|---|---|---|---|
| Working | Sky `#56B4E9` | spinner arc | slow comet sweep (~1 Hz) | none |
| Needs input | Yellow `#F0E442` | `?` bubble | gentle pulse (~1 Hz) | double tick |
| Permission | Orange `#E69F00` | hand / lock | pulsing ring, attention wake | repeating until seen |
| Done | Bluish-green `#009E73` | check | one sweep, then solid; dims after ack | soft tick |
| Error | Vermillion `#D55E00` | `!` triangle | one fast flicker, then solid | long buzz |
| Idle / snoozed | Grey `#999999` | dot / zz | none | none |
| Selected | White outline | — | — | detent tick on change |

AMOLED burn-in: only thin arcs and the centre text are lit. The idle ring dims to about 20% after 30 s.

## 5. Firmware work (from the capability map)

1. **Prerequisite for performance:** split `ui_ring.c` invalidation into per-segment areas. Today any ring change
   repaints all 466×466 pixels.
2. Drag stream from `touch_read_cb`, coalesced to the latest point, plus an angle recogniser in `gesture.c`
   (annulus, sector hysteresis of 5° past the boundary, capped at a third of a sector; a fractional margin
   lagged 40–60° with 2–4 sessions on the device).
3. New `ui_ring` screen; remove the glance, list and detail screens and the 330 ms tap deferral.
4. Button double-press and the A+B chord in `input.cpp`/`gesture.c`.
5. Haptic detent pattern (≤ 15 ms kick only) and an escalating approve ramp.
6. Measure: fps during ring animation, a `heap_caps` free log, and rim touch accuracy (open questions §6 of the
   capability doc).
7. Simulator scripts and Atlas update (the touch chapter's gesture map is generated from the firmware).

Deferred: IMU (raise-to-wake first, then optional flick to snooze; never used to approve).

## 6. Decisions

- **Rim scrub ships in v1.**
- **Focusing a Done session also acknowledges (dims) it.**
- **Snooze is skipped for now.** Long-press on Working does nothing yet (or acts as Focus). The A+B chord is reserved.
- **State colours are Okabe-Ito.** The GitHub-dark surfaces and text stay.
