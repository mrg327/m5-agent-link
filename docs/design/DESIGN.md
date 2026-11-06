# m5-agent-link: design (working draft)

> Status: **under discussion**. Inputs: user requirements (2026-11-06) and `docs/research/*.md`.
> Decisions are marked **[D]**, proposals awaiting confirmation **[P]**, and open questions **[?]**.

## 0. Guiding principles [D]

1. **Auditability for agents and humans.** An AI agent (or a human) working on or debugging this system must be able to see **timing**, **message content**, and **peripheral behaviour** without special tools.
   - **One trace format everywhere:** a JSONL stream with one event per line: `{ts_us, src, dir, kind, sid?, rid?, msg_symbolic, bytes?}`. It is written by the daemon (`~/.agentlink/trace/*.jsonl`) for each hook event, state transition, protocol message (decoded to symbolic names, plus the raw hex), focus attempt, and approval decision.
   - **The device is observable:** firmware emits structured log lines over USB CDC on a separate debug channel. These cover state changes, input/gesture events, power-state transitions, haptic/sound actions, frame time, and heap. The `ui_state` debug message reports the current screen, the selected item and visible elements. A `screenshot` debug command dumps the framebuffer as PNG over USB, from both the device and the simulator.
   - **The CLI is the agent's window:** `agentlink trace [--follow] [--sid N]`, `agentlink decode <hex>`, `agentlink device state|screenshot|tap x y|press A|lift`, and `agentlink sim …`. Each command has a `--json` output mode.
   - **A deterministic simulator** can replay a trace file and be scripted, for example "inject session X needs approval, assert the attention card is visible, screenshot". It is used in CI and by agents.
   - Approvals and focus actions are additionally written to an append-only audit log.
2. **A readable, unintimidating codebase.**
   - The project is organized around a small number of modules with obvious names. Each directory has a short `README.md` (purpose, entry points, how to test).
   - We prefer plain functions and data over deep abstraction layers. The only traits/interfaces are where variation is real (agent sources, focus drivers, transports, board HAL).
   - Files are kept small (a guideline of < 400 lines), and comments explain *why*, not *what*.
   - The protocol has a single source of truth (CDDL), and the generated code is clearly separated from hand-written code.

## 1. Vision

The device is a neck-worn agent companion that acts as an ambient control surface for AI coding agents running on the user's PC. It answers three questions without the user switching windows:

1. *Does anything need me right now?* (an approval, a question, an error)
2. *What is everything else doing?* (running, idle, or done, with elapsed time)
3. *Take me there.* A gesture brings that agent's terminal to the front.

The first device is the **M5Stack StopWatch**. The system is **device-agnostic**: other M5Stack and ESP32 boards, macropads, or a desktop/web simulator can be clients of the same daemon and protocol.

The stretch goals are approve/deny, canned replies, and push-to-talk STT. There is also a **badge mode** for showing the device off.

## 2. Requirements

| # | Requirement | Source |
|---|---|---|
| R1 | Agents: GitHub Copilot CLI, the Copilot coding agent (cloud PRs), and Claude Code | **[D]** user |
| R2 | Host OS: macOS, Windows, Linux X11, and Linux Wayland | **[D]** user |
| R3 | Terminals: Windows Terminal, macOS Terminal.app, and kitty | **[D]** user |
| R4 | Single PC in v1 (multi-PC later, so the protocol should not preclude it) | **[D]** user |
| R5 | Must work at work **without** joining the corporate Wi-Fi. "Near-device" is fine, but the user must be able to step away | **[D]** user |
| R6 | Control scope ends at voice: view, focus, ack, approve/deny, replies, and STT | **[D]** user (phased) |
| R7 | Neck-worn, so standby must be battery-conservative | **[D]** user |
| R8 | Badge mode | **[D]** user |
| R9 | The daemon is written in Rust | **[D]** user |
| R10 | **Multiple device types**: StopWatch first, others later | **[D]** user |
| R11 | Local-only: no cloud relay; prompt text never leaves the PC↔device link | **[P]** (corporate privacy) |

## 3. System architecture

```
 ┌──────────── PC ─────────────────────────────────────────────────┐
 │  copilot CLI ──hooks──┐                                           │
 │  claude code ─hooks───┼─► agentlink-hook (tiny exe) ──IPC──┐      │
 │  gh API (cloud PRs) ──┴──────────── poller ───────────────►│      │
 │                                                            ▼      │
 │                 agentlinkd (Rust, user session)                   │
 │   session registry · state machine · focus drivers · STT          │
 │   device manager (per-device capabilities, auth, transport)       │
 │        │ BLE GATT        │ USB CDC         │ localhost WS          │
 └────────┼─────────────────┼─────────────────┼──────────────────────┘
          ▼                 ▼                 ▼
     StopWatch FW      other ESP32 FW     simulator / other clients
   (shared core + per-board BSP)          (desktop window, web, etc.)
```

### 3.1 Multi-device principles (R10) [P]
1. **The daemon sends semantic data, never pixels.** It sends sessions, states, and attention events. Each device decides how to render them. This keeps the protocol small and lets very different devices work, for example a round AMOLED, a 128×64 OLED, or a single RGB LED plus a button.
2. **Capability negotiation.** On connect, the device sends `Hello{device_class, board, fw, caps}`. The `caps` field covers:
   - display: none, mono, or color; shape round or rect; width and height
   - input: buttons, touch, rotary, or IMU
   - haptic, mic, speaker, and battery
   - transports
   - max message size

   The daemon tailors traffic to these capabilities. For example, a device without a display gets no preview text. A device without a mic doesn't see the STT action.
3. **Commands are a closed, device-neutral set.** The set is `focus`, `ack`, `approve`, and so on. How a gesture or button maps to a command is decided by the device and is not part of the protocol.
4. **Firmware = portable core + board support package (BSP).**
   ```
   firmware/
     core/        protocol, session model, state, power policy hooks, UI logic (LVGL), haptic patterns
     boards/
       stopwatch/ CO5300+CST820B+M5PM1/M5IOE1+BMI270+ES8311 init, pin map, power mgmt
       <next>/    e.g. M5Dial (round 240×240 + encoder), CoreS3, AtomS3, Waveshare AMOLED
   ```
   The BSP exposes a small HAL: `display_*`, `input_*`, `haptic_*`, `audio_*`, `power_*`, `imu_*`, `link_*`. The UI layer picks a **layout profile** by shape and size (round-large, round-small, rect, or text-only) and is not tied to a board.
5. **A simulator is a first-class device.** LVGL compiled for the desktop (SDL) connects to the daemon over localhost WebSocket. It is used for UI development, CI screenshots, and as a "virtual device" for users without hardware.
6. **Protocol schema in one place.** `protocol/` holds the message definitions (CBOR with a CDDL schema, or protobuf/nanopb). Rust and C bindings are generated from it, so non-ESP clients can be written later.

Possible future devices: M5Dial (encoder is great for list scrolling), M5Stack CoreS3, AtomS3 (tiny status cube), M5StickC Plus2, an Elgato Stream Deck (handled entirely by the daemon via HID; it needs no firmware), or a phone/web page.

### 3.2 Transport: the key decision for R5 and R7

| Option | Works at work w/o Wi-Fi | Range / step away | Device battery | PC requirements | Notes |
|---|---|---|---|---|---|
| **BLE (device = peripheral)** | ✅ | ~10–30 m indoors | ✅ best (connection interval can go to ~1 s when idle) | PC Bluetooth on; **corp IT may restrict BT** | **[P] primary.** Rust `btleplug` is central on all three OSes |
| ESP-NOW via a **USB dongle** (2nd ESP32 plugged into the PC) | ✅ | 50–200 m+ | ⚠️ radio duty-cycle needed | Only a USB serial port, so no BT policy issues | **[P] fallback** if corp BT is locked down. The dongle costs about $5–8 (e.g. an AtomS3 Lite) |
| USB CDC cable | ✅ | none (tethered) | charging | none | Always available: setup, pairing, flashing, docked mode |
| localhost WebSocket | n/a | n/a | n/a | none | Simulator and software clients |
| Wi-Fi LAN | ❌ at work | building | ❌ worst | firewall prompts | Home-only option; defer |

The **transport is a framing layer.** The same protocol (§5) runs over BLE GATT, a USB CDC byte stream, a dongle that bridges ESP-NOW↔CDC, and a localhost WebSocket.

**[D] Q1.** BLE pairing works on the work machine → **BLE is the primary transport.** The dongle is deferred and kept only as a contingency.

### 3.3 Behaviour when the user steps away
- The link drops, so the device shows the "away" state and keeps the last snapshot with a "stale since hh:mm" label.
- A reconnect triggers a full snapshot resync. This is idempotent because session IDs come from the daemon.
- **[P]** The daemon buffers "attention" events while disconnected. On reconnect the device gets a single haptic summary ("2 waiting, 3 done"), not a burst.

## 4. Agent status capture

| Agent | Status source | Notes |
|---|---|---|
| Copilot CLI | **Hooks** in `~/.copilot/hooks/*.json` → `agentlink-hook` (via `exec`+`args`, no shell). The useful events are `sessionStart/End`, `userPromptSubmitted`, `preToolUse`, `postToolUse`, `permissionRequest`, `notification` (`permission_prompt`, `elicitation_dialog`, `agent_idle`, `agent_completed`), `agentStop`, and `errorOccurred` | Cross-check `~/.copilot/session-state/<id>/events.jsonl` and `inuse.<PID>.lock` (internal, unversioned) |
| Claude Code | Hooks: `SessionStart`, `UserPromptSubmit`, `PreToolUse`, `Notification`, `Stop`, `SessionEnd` | Notification does not fire in the VS Code panel |
| Copilot coding agent (cloud) | Daemon polls GitHub (`gh` auth token / GraphQL): open PRs assigned to Copilot, their agent session status, and requested reviews | Cannot be "focused" as a terminal. Instead, open the PR in the browser on the PC |

Agent sources are **plugins inside the daemon** (a trait `AgentSource`), so adding Codex/Gemini later is isolated.

The hook shim captures **terminal identity** at hook time: `TERM_PROGRAM`, `WT_SESSION`, `KITTY_WINDOW_ID`, `KITTY_LISTEN_ON`, `TMUX_PANE`, `WINDOWID`, the TTY, the PID chain, and the cwd. With these the daemon can focus the exact tab later.

### Agent state model (device-visible)
`starting → working(tool) → needs_input{approval|question} → working → idle/done | error | ended`.
Priority for sorting: `needs_input > error > done(unseen) > working > idle > done(seen)`.

## 5. Device↔daemon protocol [P]
- **Framing:** CBOR messages. Over CDC they are COBS-framed with a CRC16. Over BLE a Nordic-UART-style custom service is used (one RX and one TX characteristic) with length-prefixed fragments, MTU 247 and 2M PHY when available. Over WS, one message per binary frame.
- **Messages:**
  - `Hello{proto, device_class, board, fw, caps}` and `Welcome{host_id, host_name, proto}`
  - `Snapshot{sessions[]}`
  - `SessionUpdate{id, agent, state, title, repo, branch, tool, elapsed, summary?}`
  - `Attention{id, kind, text_preview, request_id, risk}`
  - `Cmd{focus|ack|snooze|approve|deny|reply|cancel, id, request_id, nonce}`
  - `AudioStart/AudioChunk/AudioEnd`
  - `Settings`, `Time`, `Ping`
- **Versioning:** a `proto` major/minor. Unknown fields are ignored, and messages with unknown types are dropped, so mixed device and daemon versions keep working.
- **Privacy:** by default the device receives metadata only: repo, state, tool name, and a short (≤80 char) preview. Full prompt text is shown only on an explicit "reveal" tap. **[D] Q2:** short preview by default, full text on an explicit tap.
- **Security:** pairing uses BLE LE Secure Connections with a 6-digit passkey shown on the device. On top of that, an app-layer key exchanged at pairing (Ed25519/HMAC) and a monotonic counter protect every `Cmd`, so approvals are request-bound and replay-proof. The same app-layer auth applies on every transport. Devices without a display pair over USB.

## 6. Interaction design: StopWatch (round 466×466, neck-worn)

**Physical:** The device hangs face-out. When the user lifts it, it faces the user. The IMU detects the lift and tilt, which drives wake and display rotation. **[D] Q3:** lanyard. At rest the screen faces outward, and the user lifts it to read. The device wakes on lift, and the IMU picks the orientation (0° or 180°).

**Screen map [D]:**
```
 (display off) ──lift / button / attention-glow tap──► GLANCE
   GLANCE ──swipe up──► SESSION LIST ──tap──► DETAIL
   any screen ◄── needs_input arrives & device lifted ── ATTENTION CARD
   Button B long-press ──► BADGE (visualizer)      swipe left / Button B = back
```
1. **Glance [D]:** a status ring around the bezel with one arc segment per session, colored by state (amber = needs you, red = error, green = done/unseen, blue pulse = working, grey = idle). The center shows **counts** (waiting · running · done) and a small clock. Tapping a ring segment jumps to that session.
2. **Session list:** a curved/scaling list (Wear OS style), sorted by priority (§4). Each row is **`repo · branch`** **[D]** plus an agent icon (Copilot / Claude / cloud), a state chip and the elapsed time. Rows ≥ 80 px tall.
3. **Session detail:** shows repo · branch, agent, host, current tool, elapsed time and the last summary as a short preview with full text on tap (Q2). The actions are: **double-tap = focus on PC** **[D]**, swipe right = ack, and a menu (snooze, notify-when-done, cancel).
4. **Attention card:** shown when lifted with a pending `needs_input`. It contains the agent's own prompt text (preview), plus **Approve** (hold-to-confirm arc, about 1.2 s) / **Deny** / **Open on PC** (= focus). These appear only for requests the agent raised itself (Q6).
5. **Badge:** the animated agent visualizer (§8).

**Gestures/buttons [D/P]:**
| Input | Action |
|---|---|
| Tap | Select / open |
| **Double-tap on session row or detail** | **Focus that agent on the PC** (haptic tick + "→ PC" toast) |
| Swipe up / down | Scroll list; from Glance, swipe up opens the list |
| Swipe right (detail) | Ack / mark seen |
| Swipe left | Back |
| Hold on Approve | Hold-to-confirm |
| Button A (hold) | Push-to-talk (v3); press = wake |
| Button B | Back / home; long-press = badge mode |

**Alerting while at rest [D]:** The screen faces outward on the lanyard, so text is never lit at rest. For `needs_input` and `error`, the device vibrates and shows a **brief dim colored ring glow** (about 2 s, no text, then off). Lifting it shows the attention card.

**Haptics + sound [D]:** both are on by default, with a **quiet mode** (haptics only) and **silent mode** (glow only). The mode is toggled from a quick-settings swipe-down on Glance and can also be scheduled from the daemon (e.g. during meetings, synced later from calendar/OS focus mode **[P]**).
| Event | Haptic | Sound (when not quiet) |
|---|---|---|
| needs_input | 2 long | soft two-tone chime |
| error | 3 short | low tone |
| done | 1 short | single tick |
| focus fired | tick | none |

**Voice flow (v3) [D]:** hold Button A and speak, then release. The transcript appears with a 3 s countdown ring and **auto-sends unless the user cancels** (tap = send now, swipe left/Button B = cancel, tap text = edit not possible → re-record).

## 6a. Daemon UX & approval flow [D]

**PC interface:** a tray icon (state summary, connected device, quiet mode, pause), a **minimal native config page** (devices, pairing, agents/hooks, focus-driver permissions, privacy, quiet hours) and a CLI: `agentlink pair | status | config | hooks install|remove | doctor`. Candidate crates are `tray-icon` plus `egui`/`eframe` for the small native window; this is a spike decision.

**Hook installation:** the daemon detects Copilot CLI and Claude Code and **asks once** per agent before writing hooks (`~/.copilot/hooks/agentlink.json`, the `hooks` block in Claude `settings.json`). It keeps a backup and offers `hooks remove`. Sessions that were already running pick the hooks up on restart, and the UI says so.

**Pairing:** both methods are supported:
- (a) the **BLE passkey** shown on the device and typed on the PC;
- (b) **USB-first**: plug in, then the daemon and device exchange keys over CDC, so no passkey is needed.

**Approval race, "first answer wins":**
```
agent raises permission ─► hook ─► daemon ─► device shows Attention card
                                   │
                    terminal prompt also visible to user
first of {device answer, terminal answer} wins; the other side is dismissed
```
- Ideal: the agent shows its terminal prompt **and** the hook/daemon path at the same time. When the device answers first, the daemon returns the decision to the agent. When the terminal answers first, the daemon sees it (the event stream shows `permission.completed`) and clears the card on the device.
- **Risk / spike S1:** a blocking `permissionRequest`/`PreToolUse` hook may hold the prompt back until it returns, which means the two can't run in parallel. There are two fallbacks:
  - (1) Use a **non-blocking** notification hook to show the card, and deliver the device's answer through an agent-native channel (Copilot CLI SDK/server/ACP mode, or Claude Code's equivalent) if one exists.
  - (2) Make the hook block for a short window (about 15 s), then return "ask" so the normal terminal prompt appears.
- Every device approval is logged on the PC: time, session, request, decision, and device.

**Out of range:** events are buffered while the device is out of range. On reconnect the device gets a single summary vibration and a "while you were away" list at the top of Glance.

## 7. Power budget (R7) [P]
**[D] Q4:** multi-day standby. **The display stays off until the device is lifted**, a button is pressed, or an attention event arrives. Target: **≥ 3 working days on 450 mAh** with BLE connected. Ambient mode is opt-in only.
- The display is off by default. It wakes on lift (BMI270 interrupt), on button press, or on an attention event. It auto-sleeps after 8–10 s.
- An optional **ambient mode** is a black screen with only the status ring at low brightness and pixel shift. Its cost is measured before it can become the default.
- The CPU idles in auto light-sleep. The BLE connection interval is about 1 s idle and 30 ms during interaction or audio. No Wi-Fi in normal operation.
- Power policy lives in core; the mechanisms (PMIC, wake sources) live in the BSP.


## 8. Visual language & badge mode [D]

**Palette:** inspired by GitHub dark (Primer): a true-black `#000` background (AMOLED), surfaces `#0d1117`/`#161b22`, and text `#e6edf3`/`#8b949e`. State colors follow Primer semantics:
- needs-you = attention amber `#d29922`
- error = danger `#f85149`
- done = success `#3fb950`
- working = accent blue `#58a6ff`
- idle = muted `#6e7681`
- cloud agent = done-purple `#a371f7`

We use **original** artwork only and no GitHub/Copilot logos or mascots (trademarks). The agent icons are generic glyphs.

**Badge = creature visualizer:** each live session is a small **original creature** whose species is tied to the agent type (Copilot CLI, Claude Code, or cloud agent). Its behaviour shows the state:
- working: busy, typing or bouncing
- needs you: waving, with an amber "!" bubble
- error: dizzy, red tint
- done: celebrating, then resting
- idle: sleeping "z z"

The frame shows the **handle**, the **session count**, **agent icons** and a **clock**.
- Creatures are sprite sheets: a few frames per state, about 64–96 px, in PSRAM. They are drawn as LVGL images moved by tweened paths, so the screen redraws only the dirty rectangles.
- Badge mode keeps the display on, so it is a high-power mode and shows a battery indicator.

**Easter eggs (IMU) [P]:**
- **Shake:** the creatures tumble and get dizzy, then recover.
- **Tilt:** the creatures slide or roll toward the low side, like marbles in a bowl, using a simple physics step.
- **Double-shake:** a party mode with confetti.
- Hidden: Konami-style button sequence (A A B B A B) → a credits screen.

All easter eggs are rate-limited and are off in quiet mode.

## 9. Voice / STT (stretch) [P]
- The device captures 16 kHz mono while Button A is held. It encodes with IMA-ADPCM (64 kbps) or Opus (16–24 kbps) and streams over BLE.
- The daemon runs **local** STT (`whisper-rs`, base.en/small.en). The transcript is shown on the device for confirmation before it is sent.
- **Injecting text into an agent:**
  - kitty: `kitty @ send-text`.
  - Windows Terminal and Terminal.app: there is no clean API. The options are a PTY wrapper or keystroke injection.
  - **[P]** Prefer an agent-native input path (Copilot CLI SDK/server mode) over keystroke injection. This needs a spike.

## 10. Focus drivers per OS/terminal

| Terminal | Focus method | Status |
|---|---|---|
| kitty (mac/Linux) | `kitty @ --to $KITTY_LISTEN_ON focus-window --match id:$KITTY_WINDOW_ID` + raise the OS window (requires `allow_remote_control` + `listen_on`) | **Verified live on macOS** |
| macOS Terminal.app | AppleScript: match `tty` across windows/tabs, then select the tab and bring the window to the front (Automation permission prompt) | **Verified live** |
| Windows Terminal | Walk the agent's process tree to the owning `WindowsTerminal.exe`, restore the window, `SetForegroundWindow` with the AttachThreadInput workaround. **No tab selection** (no public process→tab API) | Compiles; untested on a device |
| tmux | Select the pane (`TMUX_PANE`), then focus the outer terminal | Untested |
| Other macOS apps (iTerm, VS Code, WezTerm, Ghostty, Hyper) | Activate the app by name (`open -a`) | Untested |
| Linux X11 | `xdotool windowactivate`, falling back to `wmctrl -i -a` (by `WINDOWID`/kitty window) | Untested |
| Linux Wayland | GNOME: shell extension D-Bus; KDE: KWin script; sway/Hyprland: IPC; others: notification fallback | Not implemented |
| Cloud agent | Open the PR URL in the default browser | Not implemented |

Focus drivers are also a daemon trait (`FocusDriver`) with per-OS/per-terminal impls in `daemon/crates/agentlinkd/src/focus/`.

- **Identity capture:** `agentlink-hook` records, with every event, the terminal identity from the agent's environment (`TERM_PROGRAM`, `KITTY_WINDOW_ID`/`KITTY_LISTEN_ON`/`KITTY_PID`, `TMUX`/`TMUX_PANE`, `WT_SESSION`, `WINDOWID`, and a few more), plus the controlling `tty` and the parent pid chain (`pids`, up to 8).
- **Input policy:** that identity is untrusted (it travels hook → daemon → device → `cmd focus`). The daemon validates every value before use (tty path, kitty id, `listen_on`, tmux pane). External tools run as `Command` args with a 3 s timeout, never through a shell, and AppleScript gets values as `argv`, never interpolated.

## 11. Firmware stack [P]
ESP-IDF 5.5 + LVGL 9.5 for the core UI. This is portable to any LVGL-capable board, and the simulator uses LVGL on SDL. For StopWatch, the BSP drives the panel through M5GFX's CO5300 QSPI driver, the same approach as the factory UserDemo. The display uses two 466×48 RGB565 partial buffers in internal DMA RAM, a rounder callback for even alignment, and 30 fps with 60 fps only for small regions. NimBLE handles BLE.
Reasoning: Arduino locks the sdkconfig, and we need control of sleep, BLE, PSRAM, and cache. The factory firmware proves this stack works on this board.

## 12. Repository layout [P]
```
protocol/     schema (CDDL or .proto) + generated Rust/C bindings + test vectors
daemon/       Rust workspace: agentlinkd, agentlink-hook, agentlink-cli (pair/status), crates for sources/focus/transport
firmware/     ESP-IDF project: core/ + boards/stopwatch/ (+ future boards)
simulator/    LVGL SDL build of firmware/core with a WS transport
docs/
```

## 13. Phasing [P]

| Phase | Scope |
|---|---|
| **P0 spikes** | Copilot hook env dump; BLE from Rust on 3 OSes (+ corp-laptop BT check); CO5300 + LVGL hello world at 30 fps; power baseline; protocol schema draft |
| **v1** | Copilot CLI + Claude Code status; glance/list/detail UI; focus on kitty/Terminal.app/WT/X11; ack/snooze; haptics; BLE + USB; pairing; badge mode; simulator |
| **v2** | Cloud PR agent; guarded approve/deny; canned replies; Wayland drivers; dongle transport; 2nd board |
| **v3** | Push-to-talk STT → agent input; multi-PC; OTA |

## 14. Open questions
Resolved: Q1 BLE OK · Q2 preview+tap · Q3 lanyard · Q4 multi-day, display off · Q5 animated agent visualizer · **Q6 approvals reuse the agent's own permission rules** (the device only answers prompts the agent itself raised; no separate allow-list) · Q7 second device undecided, so design generically via layout profiles.
