# Minimal peripherals: "select a target, then act" with very few inputs

Scope: what small controllers/peripherals teach us for a 466×466 round touch AMOLED + 2 side buttons + haptics + IMU that must let the user pick an agent session and act on it in one gesture. Every claim has a URL; items marked **[unverified]** could not be confirmed in this research pass.

## 1. Per-device findings

| Device | (a) Selection | (b) Trigger / protection | (c) State display | (d) Reusable |
|---|---|---|---|---|
| **Stream Deck family** | Selection is *spatial*: each key is its own target+action; folders and pages hide extras ([Elgato comparison](https://www.elgato.com/ca/en/explorer/products/stream-deck/stream-deck-device-comparison/), [SD+ vs XL](https://www.elgato.com/uk/en/explorer/products/stream-deck/stream-deck-plus-vs-stream-deck-plus-xl/)). SD+ adds 4 dials + touch strip (same sources). | "Stream Deck is built around one-tap control"; a Multi Action chains many steps on one key, no nesting ([Elgato](https://www.elgato.com/eu/bg/explorer/products/stream-deck/what-is-a-multi-action-for-stream-deck/)). Hold-to-confirm semantics for Stream Deck keys: **[unverified]**. | LCD keys with custom icons per key ([Elgato comparison](https://www.elgato.com/ca/en/explorer/products/stream-deck/stream-deck-device-comparison/)). Multi-state icon toggles: **[unverified]**, widely known but not confirmed here. | Key = target AND action (no submenu); one tap can run a macro (e.g. focus + ack). Folders/pages are the "submenu" cost the user dislikes – avoid. Pedal: listed as 3-key, no display ([comparison](https://www.elgato.com/ca/en/explorer/products/stream-deck/stream-deck-device-comparison/)); semantic details **[unverified]**. |
| **Flic 2 / Flic Twist** | Twist: 12 detents; *Selector mode* uses **push-twist** to pick one of up to 11 devices/scenes (basic mode: 3) ([Trusted Reviews](https://www.trustedreviews.com/reviews/flic-twist), [HA docs](https://www.home-assistant.io/integrations/flic_button/)). | Flic 2: single push, double push, hold ([HA](https://www.home-assistant.io/integrations/flic_button/)). Twist: push, double-push, twist, push-twist (push-twist replaces hold) ([Trusted Reviews](https://www.trustedreviews.com/reviews/flic-twist)). Per selected target you can define push, double-push and twist actions (same). | Twist ring has 12 white LEDs for feedback ([Trusted Reviews](https://www.trustedreviews.com/reviews/flic-twist), [PCWorld](https://www.pcworld.com/article/2565785/flic-twist-smart-button-review.html)). | Cleanest precedent: **modifier-turn = select, plain press = act**, with per-target action maps. Warning: both reviews call configuration daunting ([PCWorld](https://www.pcworld.com/article/2565785/flic-twist-smart-button-review.html)) → ship fixed defaults. |
| **Surface Dial** | Press-and-hold opens radial menu; rotate highlights item ([MS Learn](https://learn.microsoft.com/en-us/windows/uwp/ui-input/radialcontroller-walkthrough)). | Click selects highlighted item (menu active) or invokes the command in the app (menu inactive) (same). Only 3 gestures: press-hold, rotate, click ([MS Learn](https://learn.microsoft.com/en-us/windows/uwp/ui-input/windows-wheel-interactions)). | Haptic tick while rotating is controllable ([MS Learn](https://learn.microsoft.com/en-us/windows/uwp/ui-input/radialcontroller-walkthrough)). | Mode-dependent meaning of the same 3 inputs; Microsoft positions it as a *secondary* device for the non-dominant hand. Radial ring suits a round screen. |
| **Logi MX Creative Console / Loupedeck** | LCD keypad with up to 15 pages per profile + dialpad (dial, roller, keys) ([Logitech](https://www.logitech.com/en-hk/products/keyboards/mx-creative-console.html)). Loupedeck specifics **[unverified]**. | "When 3 clicks become 1 press" – the value proposition is collapsing paths to one key press (same). | LCD keys give visual feedback (same). | Dial for continuous values, keys for discrete actions; paging is the cost to avoid. |
| **Logitech MX Master 4 Actions Ring** | Radial ring of "action bubbles"; navigate between bubbles ([Logitech support](https://support.logi.com/hc/en-ph/articles/34335429816727-Haptic-Feedback-MX-Master-4)). | Click a bubble triggers it. | Haptic on moving between bubbles and on trigger (same; also [Logitech haptics](https://secure.logitech.com/en-gb/software/haptics)). | Haptic tick per segment + distinct "commit" pulse – directly implementable on the vibration motor. Ring opening gesture details **[unverified]**. |
| **Teenage Engineering OP-1** | Four colour-coded encoders; a green on-screen element means the green encoder controls it ([TE layout guide](https://teenage.engineering/guides/op-1/original/layout)). | n/a (no destructive-confirm data found). | Colour-coded interface, AMOLED animation of modulated parameters ([TE overview](https://teenage.engineering/products/op-1/original/overview)). | Colour = *which control affects this*. We could tint the UI element the button/gesture will act on. Also an assignable g-force sensor for motion effects (same) – precedent for IMU use. |
| **AirPods / Pixel Buds** | None (single target). | Press = primary; double/triple = next/previous; **press-and-hold = secondary/mode** (default ANC toggle, or assistant) ([9to5Mac](https://9to5mac.com/2026/03/19/airpods-pro-now-have-10-hidden-controls-in-ios-26-heres-the-full-list/), [HowToGeek](https://www.howtogeek.com/885365/airpods-pro-controls-features-and-gestures-explained/)). Pixel Buds: tap play/pause, hold = noise control or assistant; **only touch-and-hold is customizable** ([Pixel Help mirror](https://googlesupport.serverhump.com/googlepixelbuds/answer/7560934?hl=en&ref_topic=7544331), [Digital Trends](https://www.digitaltrends.com/home-theater/how-to-customize-pixel-buds-pro-touch-controls/)). Same button maps to context: stem press = answer call vs. play/pause; double = end call vs. next ([9to5Mac](https://9to5mac.com/2026/03/19/airpods-pro-now-have-10-hidden-controls-in-ios-26-heres-the-full-list/)). Head nod/shake answers Siri (same). | Audio chime / tone (Pixel: "release after chime" for notifications, per Pixel Help mirror). | **Context-default action** on a single press (state decides meaning), hold for secondary, 1/2/3 count ladder. Head nod = yes / shake = no is a candidate IMU confirm. |
| **Pebble / Garmin** | Up/Down move through lists, Select accepts, Back goes up ([Pebble dev](https://developer.repebble.com/guides/events-and-services/buttons/)). | Repeat-while-held for scrolling; long press (default delay ~700 ms in example) and multi-click recognizers ([Pebble Alloy](https://developer.repebble.com/guides/alloy/sensors-and-input/)); ActionBar hints label what each button does (Pebble buttons page). Garmin: **[unverified]** (not retrieved). | ActionBar icons next to buttons. | Fixed button roles + on-screen hint labels beside the physical buttons → perfect match for our 2 side buttons. |
| **Busylight / Luxafor / Embrava / Kuando** | None (one status for one person). | n/a – "light shows your status; it cannot change it" ([slack.green](https://slack.green/en/blog/teams-status-light)). | Red busy/on call, yellow/orange away, green available, **blue incoming call/alert (can flash)**, purple DND/focus, white custom ([Kuando](https://busylight.zendesk.com/hc/en-us/articles/33450271411858-Busylight-Color-Scheme)). Teams-style green=available, red=call/DND, yellow=away ([slack.green](https://slack.green/en/blog/teams-status-light)). | Conventional palette users already know; flashing = needs attention. Hue/Nanoleaf colour conventions: **[unverified]**. |
| **AI-agent status lights (DIY)** | Aggregate only (worst state wins): priority blocked > permission > attention > working > idle ([esp32-claude-status-light](https://github.com/lianshuang-photo/esp32-claude-status-light/blob/main/README.md)). | None – display only. | Green idle, yellow attention, red blocked, three-colour pulse = working (same). Another: yellow breathing = prompt submitted, slow blink = tool running, solid green = done, fast red blink = error, alternating red/yellow = waiting for approval ([AI-Status-Light](https://github.com/zxLumen/AI-Status-Light)). | Validates our state→animation mapping; shows prior art lacks per-session select/act – our differentiator. "Copilot Deck"/Hackaday projects: **[unverified]**, not found. |

## 2. Cross-cutting patterns

1. **Context-default press**: one press means whatever the current state implies (AirPods call vs. media; Surface Dial in-menu vs. in-app).
2. **Hold = secondary / deliberate**: universal across earbuds, Flic, Surface Dial, Pebble.
3. **Count ladder** (1/2/3 presses) is common but error-prone for dangerous actions.
4. **Modifier + rotate = select** (Flic push-twist) keeps plain press free for the action.
5. **Haptic detent per item + distinct commit pulse** (Actions Ring, Surface Dial).
6. **Persistent hints** beside buttons (Pebble ActionBar) replace menus.
7. **Colour = state or control affinity** (Busylight, OP-1), always with motion for urgency.

## 3. Candidate interaction models (466 px round, 2 buttons, haptics, IMU)

Per-state default actions used below ("press" = main action; "hold" = secondary):

| Session state | Press (default) | Hold (≥600 ms, ring-fill) | Notes |
|---|---|---|---|
| Working | Focus terminal | Snooze/mute updates for this session | Nothing destructive |
| Done | Acknowledge/dismiss (ack clears the badge) | Focus terminal | Dismiss is reversible |
| Needs input (generic) | Focus terminal | Quick-reply "continue" | |
| Permission request | Focus terminal (review) | **Hold = Approve** (progress ring + escalating haptic); **Deny** on other button hold / shake | Never approve on tap |
| Error | Focus terminal | Dismiss | |

### Model A – "Ring carousel + context press" (recommended)
Swipe/rotate-drag around the bezel (or button 1/2 = prev/next) to select; the focused session fills the centre, with a coloured arc. Tap centre or button A = context-default; hold = secondary. Haptic tick per session.
Pros: zero submenus; list and detail merge; Flic/AirPods precedent. Cons: a long list needs scrolling; tap-vs-hold needs clear affordance.

### Model B – "Two buttons = two verbs"
Touch selects (tap a ring segment, one segment per session). Button A = primary action, Button B = secondary; hold either = confirm-protected action (Pebble fixed roles + hint labels). Labels drawn at the screen edge next to each button, recoloured per state.
Pros: eyes-free, reliable with gloves/wet hands, matches Pebble. Cons: only two verbs; state-dependent meaning must be taught by labels.

### Model C – "Radial action flower" (Surface Dial / Actions Ring)
Tap a session → 3–4 coloured action petals appear around it; swipe toward a petal and release to fire (flick gesture), hold on petal to confirm destructive ones.
Pros: rich, discoverable, uses round screen. Cons: this is still a (small) submenu – two steps, slower than A/B; use only for rarely needed actions.

### Model D – "IMU confirm layer"
On top of A/B: wrist-flick/tilt toggles snooze; nod-style double-tilt or shake = deny; press-and-hold + steady = approve. Head-gesture precedent exists in AirPods ([9to5Mac](https://9to5mac.com/2026/03/19/airpods-pro-now-have-10-hidden-controls-in-ios-26-heres-the-full-list/)); wrist equivalents for a wearable are untested **[unverified]**.
Pros: hands-busy approval, fun. Cons: false triggers on a wearable; never sole confirm for approvals.

### Model E – "Aggregate-first glance with per-session peek" (Hue-light style)
Home shows worst-state colour (priority like [esp32-claude-status-light](https://github.com/lianshuang-photo/esp32-claude-status-light/blob/main/README.md)); button A jumps directly to the highest-priority session and acts with its default. Pros: fewest inputs for the common "something needs me" case. Cons: hides other sessions; combine with A.

**Recommendation:** A as the base, B's edge labels for the buttons, E's priority jump as the button-A shortcut, D only as an optional deny/snooze accelerator.

## 4. Colour / state palette (colour-blind safe)

Based on the Okabe-Ito palette, designed to stay distinguishable under deuteranopia/protanopia/tritanopia, with authors' advice to avoid colour-only encoding ([sci-draw](https://sci-draw.com/figure-accessibility-kit), [Vizcept](https://vizcept.com/blog/okabe-ito-palette-guide)). Convention anchors: green = available, red = busy/blocked, yellow/orange = away/attention, blue = alert/incoming ([Kuando](https://busylight.zendesk.com/hc/en-us/articles/33450271411858-Busylight-Color-Scheme)).

| State | Colour | Hex | Shape / icon | Motion | Haptic |
|---|---|---|---|---|---|
| Working | Sky blue | #56B4E9 | rotating arc / spinner | slow breathing (~1 Hz) | none |
| Done | Bluish green | #009E73 | check mark | one-shot sweep, then solid | single soft tick |
| Info / needs input | Yellow | #F0E442 | speech bubble / "?" | gentle pulse (~2 Hz) | double tick |
| Permission / attention | Orange | #E69F00 | hexagon + hand/lock | pulsing ring + fill-to-confirm | repeating buzz until seen |
| Error | Vermillion | #D55E00 | triangle / "!" | fast flicker (~4 Hz) once, then solid | long buzz |
| Idle / snoozed | Grey | #999999 | dim dot / zzz | none | none |
| Selected / focus | White ring | #FFFFFF | thick outline | – | tick on change |

Notes: green vs. vermillion/orange differ in luminance and are backed by distinct icons, so they do not rely on hue (WCAG's "colour not the only means" principle – not fetched, **[unverified]** citation). Use a deliberate fast-blink only for errors/permission; keep working animations calm to limit AMOLED burn-in and distraction. Avoid pure red/green pairs; avoid blue-vs-purple adjacency. Test with a CVD simulator ([Vizcept](https://vizcept.com/blog/okabe-ito-palette-guide) recommends it). Colour-as-control-affinity (OP-1) can be applied by tinting the button hint label with the state colour ([TE layout](https://teenage.engineering/guides/op-1/original/layout)).

## 5. Gaps

Not verified: Stream Deck hold/multi-state toggle and Pedal behaviour, Loupedeck specifics, Actions Ring invocation gesture, Garmin button model, Hue/Nanoleaf colour conventions, Copilot-Deck/Hackaday gadgets, WCAG citation, Pixel Help mirror is a third-party copy of Google's page.
