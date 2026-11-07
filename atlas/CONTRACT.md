# Atlas: build contract

The Atlas is the project's living, visual explainer. It is a static site with no build
step. The root `index.html` is the entry point, and everything else lives in `atlas/`.
Serve it with `scripts/atlas` (`python3 -m http.server`). Browsers block ES modules and
WASM on `file://`, so `index.html` shows a hint when it's opened from disk.

## Rules
- Use vanilla ES modules, CSS and SVG. Add no npm runtime dependencies and no CDN. Fonts
  are the system stack or self-hosted in `atlas/assets/`.
- Third-party code is vendored into `atlas/vendor/` with its licence. Keep it to a minimum.
- Must work in current Chrome, Safari and Firefox, on desktop and mobile.
- Respect `prefers-reduced-motion`. Use semantic HTML, keyboard access and ARIA labels on
  interactive SVG.
- Facts (thresholds, colours, UUIDs, timings, message names) come from
  `atlas/data/facts.json`. That file is generated from source by `atlas/tools/extract.py`;
  never hand-copy a constant. If a fact you need is missing, ask the owner of `tools/` to
  add it, or add a clearly marked `TODO(fact)` fallback.
- Every module may use only the files it owns (table below) plus the shared APIs here.

## Page structure (`index.html`, owned by **shell**)
One long narrative page with a persistent watch "stage". On wide screens the stage is
sticky on one side, and the narrative chapters scroll past it and drive it. Chapters, in
order, each a `<section data-chapter="id">`:

| id | Chapter | Owner module |
|---|---|---|
| `intro` | Hero: what it is, the watch in its best light | shell (`js/stage/*`) |
| `glance` | What the watch shows: the ring anatomy, states, colours | shell |
| `flow` | How data travels: agent → hook → daemon → USB/BLE/WS → watch | flow (`js/flow/*`) |
| `touch` | How you talk to it: gestures, timing, haptics, focus-on-PC | gestures (`js/gestures/*`) |
| `replay` | Your own data: drop a trace file and replay real sessions | gestures (`js/replay/*`) |
| `build` | Under the hood: firmware/daemon/protocol map, links to docs | shell |

Chapter modules export `export function mount(sectionEl, ctx)`, which is called once at
start-up, plus an optional `export function onEnter(ctx)` and `export function onLeave(ctx)`
that the shell calls on scroll.

## Shared context `ctx` (created in `atlas/js/main.js`, owned by **shell**)
```js
ctx = {
  facts,          // parsed atlas/data/facts.json
  bus,            // atlas/js/bus.js: on(type, fn) → off(); emit(type, detail)
  watch,          // WatchRuntime (below), mounted inside the stage
  daemon,         // MockDaemon (below), attached to ctx.watch
  stage,          // Stage API (below)
  reducedMotion,  // boolean
}
```

### Bus events (any owner may emit or listen)
| type | detail | emitted by |
|---|---|---|
| `watch:tx` | `{bytes: Uint8Array, msg}` frame the watch sent (msg = decoded CBOR) | daemon |
| `watch:rx` | `{bytes, msg}` frame delivered to the watch | daemon |
| `watch:haptic` | `{steps: [{on_ms, off_ms, duty}]}` | watch |
| `watch:gesture` | `{kind: 'tap'|'long_press'|'scrub'|'select'|'chord'|'swipe_l'|'swipe_r'|'swipe_u'|'swipe_d'|'btn_a'|'btn_b', x, y}` | watch |
| `watch:screen` | `{screen: 'ring'|'attention', sel}` | watch |
| `pc:focus` | `{sid, ok}` the mock PC brought a terminal forward | daemon |
| `scenario:step` | `{name, step, text}` | daemon |
| `transport:set` | `{kind: 'usb'|'ble'|'ws'}` which link the story shows | flow |

## WatchRuntime (`atlas/js/watch-runtime.js`, owned by **wasm**)
```js
const watch = await createWatch(canvasEl, { bus });  // 466×466 canvas
watch.kind            // 'wasm' (real firmware core + LVGL) or 'stub'
watch.pointer(type, x, y)  // type: 'down'|'move'|'up', coordinates in canvas px
watch.button(id, down)     // id: 'a'|'b'
watch.deliver(bytes)       // inbound protocol frame (CBOR envelope bytes)
watch.onTx(fn)             // fn(bytes) for outbound frames
watch.setLinkUp(kind, up)  // kind: 'usb'|'ble'|'ws'
watch.reset()
```
If the WASM build is missing, `createWatch` falls back to a stub that draws an honest
placeholder and still emits events. Don't fake firmware behaviour in the stub.

## MockDaemon (`atlas/js/proto/mock-daemon.js`, owned by **flow**)
A JS implementation of the daemon side of the protocol, enough to drive the real watch
core: it answers hello with welcome and a snapshot, sends session deltas, answers ping and
cmd. Scenarios script fake agents (working → needs input → done ...).
```js
const daemon = createMockDaemon({ bus, facts });
daemon.attach(watch); daemon.play(name); daemon.pause(); daemon.scenarios  // [{name, title}]
daemon.inject(sessionPatch)  // used by replay
```
Codecs live in `atlas/js/proto/` (cbor.js, cobs.js, crc16.js, blefrag.js, messages.js).
They are pure functions with tests in `atlas/test/` (`node --test atlas/test/*.test.mjs`).

## Stage API (owned by **shell**)
```js
ctx.stage.focus(region)        // 'screen'|'buttons'|'motor'|'usb'|'antenna'|null: spotlight a hardware part
ctx.stage.annotate(id, {x, y, text} | null)  // callout on the watch, in canvas coords
ctx.stage.trace(points)        // draw a fading finger trail over the screen [{x,y,t}]
ctx.stage.pulse(kind)          // visual haptic/radio pulse: 'haptic'|'usb'|'ble'
ctx.stage.setMode('hero'|'side'|'mini')
```
`main.js` sets the mode from each section's `data-layout` (`hero`, `side`, or `wide` → mini),
and mirrors it on `body[data-layout]`. A chapter may override it in `onEnter`.

## Maintaining the Atlas
- Serve it with `scripts/atlas` (it uses `atlas/tools/serve.mjs` under node, otherwise python3).
- Firmware UI change: rebuild the WASM watch with `atlas/wasm/build.sh` and commit `atlas/vendor/watch.*`.
- Constants changed (palette, gesture timings, haptics, protocol): run `python3 atlas/tools/extract.py`.
  CI (`.github/workflows/atlas.yml`) runs `--check` and `node --test atlas/test/*.test.mjs`.
- Hand-written prose in the chapters (for example the gesture→action matrix) must be updated by hand
  when interaction design changes (see `docs/design/UI-RING.md`).

## File ownership
| owner | files |
|---|---|
| shell | `index.html`, `atlas/css/base.css`, `atlas/css/stage.css`, `atlas/css/chapters.css`, `atlas/js/main.js`, `atlas/js/bus.js`, `atlas/js/stage/**`, `atlas/assets/**`, `scripts/atlas` |
| wasm | `atlas/wasm/**`, `atlas/js/watch-runtime.js`, `atlas/vendor/watch.*` |
| flow | `atlas/js/flow/**`, `atlas/js/proto/**`, `atlas/css/flow.css`, `atlas/test/proto_*.test.mjs` |
| gestures | `atlas/js/gestures/**`, `atlas/js/replay/**`, `atlas/css/touch.css`, `atlas/tools/**`, `atlas/data/**`, `atlas/css/tokens.generated.css`, `atlas/test/facts_*.test.mjs`, `.github/workflows/atlas.yml` |
| main agent | this file, `README.md` link |

`atlas/css/tokens.generated.css` is written by `extract.py` and holds the firmware's real
palette and timings as CSS variables (`--fw-*`). `base.css` imports it and defines the
site's own art direction on top.

## `facts.json` schema (minimum; the gestures owner may add keys but must not rename these)
```jsonc
{
  "generated_from": "<git sha or 'worktree'>",
  "palette": { "bg": "#000000", "surface": "#0d1117", "surface2": "#161b22", "border": "#30363d",
               "text": "#e6edf3", "muted": "#8b949e",
               "working": "#56b4e9", "ask": "#f0e442", "permission": "#e69f00",
               "done": "#009e73", "error": "#d55e00", "idle": "#999999",
               "amber": "#e69f00", "danger": "#d55e00", "success": "#009e73", "accent": "#56b4e9" },
  "states": { "<state id as in the CDDL/model>": { "color": "#hex", "label": "Working", "palette": "working", "source": "file:line",
              "sub_labels": { "0": "Permission" }, "sub_colors": { "0": "#e69f00" } } },  // sub_* only on `input`
  "gesture": { "long_press_ms": 600, "swipe_min_px": 60, "swipe_max_ms": 400, "tap_slop_px": 18,
               "btn_double_ms": 300, "cx_px": 233, "cy_px": 233, "centre_r_px": 150,
               "rim_in_px": 160, "rim_out_px": 260, "rim_track_in_px": 110, "rim_track_out_px": 340,
               "scrub_start_deg": 10, "scrub_hyst_deg": 5, "hold_confirm_ms": 1200, "hold_show_ms": 250 },
  "ring": { "merge_at": 9, "ambient_ms": 30000, "nag_ms": 15000, "nag_max": 3, "hint_a_deg": 225, "hint_b_deg": 135,
            "repo_w_px": 250, "repo_px": 32, "repo_small_px": 24,
            "kinds": { "<work|ask|perm|done|error|idle>": "shape and motion text" },
            "pills": { "approve": "HOLD ✓ APPROVE", "done": "FOCUS + ACK", "default": "FOCUS" },
            "edge_hints": { "a": "FOCUS", "b": "NEXT" } /* plus geometry and per-kind periods */ },
  "gesture_map": { "cases": [ { "gesture": "G_TAP", "source": "file:line", "text": "..." } ], "chord": "...", "first_touch_wakes_only": true },
  "screens": [ "ring", "attention" ],
  "haptics": { "<detent|hold_ramp|tick|confirm|reject|hello|done|error|question|permission>": [ { "on_ms": 35, "off_ms": 0, "duty": 100 } ] },
  "link":   { "hello_retry_ms": 3000, "ping_interval_ms": 5000, "silence_timeout_ms": 12000 },
  "usb":    { "vid": "0x303A", "pid": "0x1001", "crc": "CRC-16/CCITT-FALSE", "max_payload": 4096 },
  "ble":    { "service": "uuid", "rx": "uuid", "tx": "uuid", "name_prefix": "AgentLink-", "default_write_len": 20 },
  "display":{ "width": 466, "height": 466, "shape": "round" },
  "messages": [ { "name": "hello", "dir": "dev→pc", "summary": "..." } ],
  "sources": { "<key>": "file:line" }
}
```
The values above are examples. Every value must be extracted from source, never copied from
here.
