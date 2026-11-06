# Similar products and open-source projects

Research date: 2026-11-06. Scope: hardware companions for AI coding agents, general desk/status devices, and software patterns for aggregating agent status.
Items marked **(unverified)** come from search snippets or general knowledge and were not confirmed by reading the primary source.

## 0. Bottom line

- The idea is **not new**. At least six open-source projects already put coding-agent status (and approve/deny) on physical devices. The closest are **AgentDeck**, **nimbus-notify**, **edrethardo/agent-deck**, **Anthropic's claude-desktop-buddy**, **Copilot Deck** (Stream Deck plugin for Copilot CLI), and **Hermes Gadget SDK** (which already supports the M5Stack StopWatch as a voice terminal).
- Nobody found so far combines all of: round AMOLED wearable + multi-PC daemon + multi-agent (Copilot CLI first) + touch-to-focus-the-right-window-on-the-right-PC + haptics.
- The reusable mechanism is the same everywhere: **agent lifecycle hooks → local daemon → small JSON/binary protocol → device**, with permission hooks that *hold the HTTP response open* until the human answers.

---

## 1. Hardware companions for AI coding agents

### 1.1 AgentDeck (puritysb) — closest overall competitor
- URL: https://github.com/puritysb/AgentDeck (README: https://raw.githubusercontent.com/puritysb/AgentDeck/master/README.md; device catalog: https://raw.githubusercontent.com/puritysb/AgentDeck/master/docs/devices.md)
- Offers: local daemon + dashboards for Claude Code, Codex CLI, OpenCode (Hermes preview). Surfaces: Stream Deck/Stream Deck+, Ulanzi D200H/X, iPad/iPhone/Mac/Android apps, e-ink (TRMNL, XTeink), ESP32 panels (TTGO T-Display, LilyGO T-Embed CC1101 knob, T-Display-S3-Pro), Pixoo64/Timebox LED matrices, TUI (`agentdeck dashboard`), Apple Watch notification mirror (CloudKit PoC, observation only).
- Form factors: wide range; the **T-Embed** is a dual-mode knob/pager (USB on desk, 1300 mAh battery as "carry-around pager that chimes when a session starts waiting"; 8-LED ring = one LED per session).
- Comms: one **daemon hub on port 9120**; devices speak WebSocket JSON; ESP32 also USB-CDC serial JSON (115200) with a WiFi token that is **provisioned over serial**; discovery via mDNS / port scan / QR. Hooks for Claude Code and Codex, plus rollout logs. Multi-machine: `--remote-daemon --daemon-host mainnode.lan` attaches sessions from other machines to a main node (opt-in, local-only by default).
- Config/pairing: `agentdeck daemon install` installs hooks and starts the daemon; token auth with local bypass.
- License/price: MIT, free. Apps on App Store / Google Play / Elgato Marketplace / Ulanzi Marketplace.
- UX ideas: "attention / working / done" states; explicit **"read-only unless the exact request is bound to the response"** rule (approvals only offered on devices that can show the full subject; subject >159 UTF-8 bytes, >6 options or a >63-byte label stays read-only on compact screens); ambient-light dimming; a living "aquarium" for glanceability.
- Relevance: validates architecture; multi-machine remote attach is its weakest/experimental part ("`agentdeck relay`, experimental"). No Copilot CLI listed in the observed table (verified only for Claude Code, Codex, OpenCode, Hermes).

### 1.2 nimbus-notify (ristllin)
- URL: https://github.com/ristllin/nimbus-notify ; protocol: https://raw.githubusercontent.com/ristllin/nimbus-notify/main/docs/protocol.md ; PyPI https://pypi.org/project/nimbus-notify/
- Offers: Python host broker watching Claude Code, Codex, Mistral Vibe via hooks (Vibe via log watcher); pushes per-session state to an LED ring / e-ink device. MIT.
- Comms: serial (USB-CDC) or BLE using a documented binary protocol **nsn**: `[0xAA][LEN][payload][CRC8]`, payload = magic, seq, segment count, brightness, then 4-byte segment records (state, hue, anim, span). Each frame is a full snapshot ("latest wins"), up to 16 segments.
- States: Idle, Running, WaitingInput, AwaitingApproval, Done, Error, Offline; defaults: Running blue/comet, WaitingInput purple/breathe, AwaitingApproval amber/blink, Done green/fade, Error red/solid.
- Notable: `heartbeat` and `wakeup` events so unattended sessions do not pin a "needs you" state forever; permission is never downgraded to a plain question; idle TTL reaper.
- Hook-facing CLI `led-report <harness> <verb>`; events to broker over a Unix socket as JSON lines.

### 1.3 edrethardo/agent-deck — "physical traffic light" macropad
- URL: https://github.com/edrethardo/agent-deck
- Offers: each Claude Code session on a key of a DeepDeck 16-key WiFi macropad (ESPHome firmware). Green idle, yellow working, red blocked, blue "detected before hooks installed". OLED shows project; knob dims LEDs.
- Gestures: press = see which project; **double-press = jump to that session's window on the desktop**; hold = rearrange.
- Comms: PC daemon talks to the pad over LAN via ESPHome encrypted native API (aioesphomeapi); Claude hooks → Unix socket → daemon (systemd user service); /proc scan for sessions started before hooks.
- Open source; hardware ~hobbyist macropad.

### 1.4 Anthropic claude-desktop-buddy (M5StickC Plus desk pet)
- URL: https://github.com/anthropics/claude-desktop-buddy (README raw: https://raw.githubusercontent.com/anthropics/claude-desktop-buddy/main/README.md); summary: https://hysenlabs.com/projects/anthropics-claude-desktop-buddy
- Offers: official maker reference. Claude desktop app (macOS/Windows) in Developer Mode exposes a **BLE Nordic UART Service JSON API** for permission prompts and recent messages; device approves/denies with buttons. ~2.6k stars at time of summary.
- Pairing: Help → Troubleshooting → Enable Developer Mode → Developer → Open Hardware Buddy → Connect; auto-reconnect.
- UX: seven states (sleep, idle, busy, attention [LED blinks], celebrate, dizzy, heart); 18 ASCII pets; GIF character packs pushed over BLE (<1.8 MB, 96px wide); button A = approve, B = deny in the approval screen; shake and face-down gestures via IMU; "heart" if approved in <5 s; screen stays on while approval pending.
- Caveat: only for the desktop app; not a supported product; protocol spec in `REFERENCE.md` (https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md, not read).

### 1.5 Permitter (ericfriedman) — M5Stack Core2 permission panel
- URL: https://github.com/ericfriedman/permitter/blob/main/PERMITTER.md
- Offers: Claude Code PreToolUse hook → bridge server → M5Stack Core2 over WiFi (HTTP); three touch zones: **Trust Always / This Time / Deny**; "solo mode" where the device is the sole gatekeeper (pre-allows tools in settings so only the hook decides); trusted set resets with bridge restart; runtime-switchable themes. Open source (Arduino/C++, ArduinoJson).

### 1.6 CVSRohit/claude-code-remote — ESP32 remote with cloud relay
- URL: https://github.com/CVSRohit/claude-code-remote
- Offers: ESP32 + 1.3" ST7789 240×240 + 4 buttons (▲ ▼ ✓ ✗); launch prompts, approve/deny every tool call, scroll output, interrupt.
- Comms: device → wss/TLS → Fly.io relay ← outbound wss from a PC runner (Claude **Agent SDK** with `canUseTool` callback). Token-paired; single device + single runner per token. Printable 3D case. Warns that runner uses `bypassPermissions`, so token = shell key.

### 1.7 Apple Watch controllers
- `shobhit99/claude-watch` — Swift, Node bridge on Mac, live tool output on watch, approve prompts from wrist; ~626 stars (per https://hysenlabs.com/projects/shobhit99-claude-watch). Bridge blocks on the hook until answered. License unclear.
- `CryptoPilot16/watch-control` — https://github.com/CryptoPilot16/watch-control: approve **Codex and Claude Code** from Apple Watch; agent runs in tmux on a remote Linux server; Codex flow polls tmux every 2 s to detect the prompt and injects `y`; Claude flow uses the PermissionRequest hook; iPhone + watchOS app; optional Pushover fallback.

### 1.8 Hermes Gadget SDK (explicit StopWatch support)
- URL: https://github.com/allinnb/hermes-gadget-sdk (fork of https://github.com/Adolanium/hermes-gadget-sdk); docs: https://raw.githubusercontent.com/allinnb/hermes-gadget-sdk/main/docs/supported-hardware.md, `docs/protocol.md`.
- Offers: ESP32-S3 / Raspberry Pi "voice terminal" for **Hermes Agent** (Nous Research): hold to talk, hear reply, see cards, answer confirmation questions, expose LEDs/sensors as agent tools. Not a coding-agent status board, but a reusable device framework. MIT.
- StopWatch mapping (supported-hardware.md): **blue button hold-to-talk; yellow cancel/navigation; red screen sleep/wake, hold for Settings**; analog mic + speaker; "experimental battery/power reporting". Ports are "experimental until a physical verification report exists".
- Protocol v1: one **WebSocket per device** (`ws://host:8765/gadget`, subprotocol `hermes-gadget.v1`), JSON text frames + 4-byte-header binary frames (audio PCM16, RGB565 images, firmware). Handshake `hello → challenge → auth → welcome → pairing(code, "approve <CODE>") → paired`; device key (32 random bytes in NVS), `device_id = "hg-"+sha256(key)[:16]`; devices advertise `caps` (display size, mic, speaker, inputs, talk_mode) and `actions` (agent-callable tools). Browser-based USB installer (Chrome/Edge, Web Serial), phone-based Wi-Fi provisioning via temporary AP, OTA with auto-rollback if Hermes unreachable, desktop simulator with same C++17 core, Tailscale Funnel for remote access, Home Assistant/MQTT examples.
- Relevance: strongest source of reusable ideas for pairing, capability negotiation, simulator, OTA, and StopWatch pin/button mapping.

### 1.7b M5Stack StackChan / Xiaozhi
- StackChan: https://github.com/m5stack/StackChan — MIT, firmware + iOS/Android app + Go server (WebSocket forwarding between devices and app clients, XiaoZhi integration, device activation, MySQL, Docker/Kustomize). CoreS3-based desktop robot with servos, 2" touch display, 550 mAh battery. Hardware page: https://docs.m5stack.com/en/core/StackChan_Core.
- Xiaozhi: https://github.com/78/xiaozhi-esp32 — MIT, MCP-based voice chatbot firmware, 70+ boards incl. M5Stack CoreS3/AtomS3R; protocols documented: WebSocket (`docs/websocket.md`), MQTT+UDP hybrid (`docs/mqtt-udp.md`), MCP device-side (`docs/mcp-protocol.md`); cloud-side MCP can do "PC desktop operation"; community servers in Python/Java/Go; web asset generator for wake words/fonts/emojis. Relevance: voice stack and MCP tool exposure, not coding-agent status.

### 1.9 Stream Deck plugins for agents
- **Copilot Deck** (Bouncing Ball) — https://marketplace.elgato.com/product/copilot-deck-fe397e6a-5052-4497-875e-741cabca39f2 (v1, Oct 7 2026). **Directly targets GitHub Copilot CLI** on macOS: session tiles, approve permission requests from a key, send prompts, change model/effort/mode, quota/forecast on dials and Neo infobar; keys **blink while Copilot is waiting**, optional sounds + macOS notification; "jumping to a session brings its terminal to the front" (Accessibility permission; Terminal/iTerm tabs precise, others only app-level). Works through a **small Copilot CLI extension** (user-approved) that reports tool/model/context/quota and answers permission requests. No cloud, no tokens.
- **copilot-stream-deck** (haflidif) — https://github.com/haflidif/copilot-stream-deck, design notes https://github.com/haflidif/copilot-stream-deck/blob/main/docs/integration.md : for the *GitHub Copilot app*. Uses an experimental extension (`joinSession()`), a loopback WebSocket companion API with bearer tokens in `~/.copilot/run/*.json` (voice toggle, `question.answer`, `session.open`), 5 s poll fallback. Findings worth reading: idle runtimes are reaped ~10 min after idle (`idle_cli_reap_check`), so sessions may have no live bridge; reads `events.jsonl` as a degraded fallback; **never auto-approves host permissions**; clears stale data on disconnect so tiles cannot act on stale state; per-session bridge on ephemeral port with random token.
- **agentsd** (paultyng) — https://github.com/paultyng/agentsd: Claude Code HTTP hooks → local server :9200 → Stream Deck keys/dials; PermissionRequest hook holds response up to 120 s; macOS only; MIT.
- **AgentDeck** — see 1.1 (Elgato + Ulanzi marketplaces).
- Blog on the pattern: https://nick-liu.com/posts/permission-round-trip/ — holds HTTP open ~590 s (Claude hook timeout 600 s) so a Stream Deck key can approve; gotchas below.

### 1.10 Host-side notifiers (software, closest to "approve from phone")
- claude-ntfy-hook — https://github.com/nickknissen/claude-ntfy-hook: ntfy push with Allow/Deny action buttons, Tailscale for callback, reads Claude ask/allow/deny rules to avoid noise, parses transcript for context, retries with backoff.
- claude-notify-everywhere — https://github.com/hwwn/claude-notify-everywhere: Stop/PermissionRequest/Notification hooks → macOS banner (click to focus Ghostty/iTerm2/Warp/VS Code/Cursor...) + ntfy/Bark/Pushover/Telegram.
- steryereo/claude-hooks — https://github.com/steryereo/claude-hooks: click-to-focus via `$TERM_PROGRAM` / `$ITERM_SESSION_ID`, auto-suppress when that window is already focused.
- achowba/ai-agent-notifications-hooks — https://github.com/achowba/ai-agent-notifications-hooks: Claude + Codex, walks process tree to identify host app, per-tool/per-event/per-scope toggles, suppresses permission notices unless system idle ≥5 s.
- Claude Code Remote Control (official): `/remote-control` exposes a local session to the Claude mobile app / claude.ai/code over an outbound connection (needs claude.ai login, machine awake) — https://arsentev.ai/guides/claude-code-phone-remote .

---

## 2. General desk-companion and status devices

| Product | Form factor | Host comms | Config / SDK | Price / license | UX ideas |
|---|---|---|---|---|---|
| Elgato Stream Deck (MK.2, Mini, Neo, +, XL) — https://streamer.guide/gears/decks , https://frontdeskreview.com/gear/stream-decks/ | 6–32 LCD keys, Neo infobar + 2 touch points, + has dials/touch strip | USB (HID-like) to Elgato app; plugins via Node SDK over local WebSocket (`@elgato/streamdeck`) | Elgato app + Marketplace; profiles per app | Proprietary; Mini ≈ $60, MK.2 ≈ $150, + ≈ $200 (US list, 2026) | Per-key live icons, dial per-context, multi-actions, app-aware profiles |
| Loupedeck / Logitech MX Creative Console — https://www.logitech.com/en-us/shop/p/buy-mx-creative-console | LCD keypad + dialpad | USB / Bluetooth via Logi Options+ | Plugin marketplace in Options+ | ≈ $200 | Pages per app, analog dial |
| Ulanzi D200/D200H/D200X | LCD key decks | USB; Ulanzi Studio plugins | Marketplace (AgentDeck supports) | ~Stream Deck alternative **(unverified price)** | Cheaper alternative |
| Luxafor Flag — https://luxafor.com/product/flag/ , https://docs.rs/luxafor/latest/luxafor/ | USB busy light, magnetic | USB HID, **webhook API**, Bluetooth models; Teams/Zoom/Slack/Zapier integrations | Free app; open API | ≈ $32–40 | One colour = one state; ambient visibility |
| Busylight (Kuando Omega), Embrava Blynclight | USB busy/ringer lights | USB HID; vendor SDK; community libs **(unverified)** | | ~$50–100 **(unverified)** | Colour + tone/ring for call alerts |
| M5Stack Dial v1.1 — https://shop.m5stack.com/products/m5stack-dial-v1-1 | 1.28" round touch, rotary encoder, buzzer, RFID, ESP32-S3 | WiFi/BLE (firmware you write) | Arduino/UiFlow/ESP-IDF/PlatformIO | $34.90 | Encoder scroll + press; round UI |
| SmartKnob (scottbez1) — https://github.com/scottbez1/smartknob | BLDC haptic knob with display | USB serial / Web Serial; ESP32-S3 | Protobuf messages, PlatformIO | Apache-2.0 / CC-BY; DIY BOM | Virtual detents and endstops; haptic "weight" for risky actions |
| Touch Portal — https://www.touch-portal.com/ | Phone/tablet as deck | LAN or USB to desktop app | Plugin API; HTTP/webhook actions | Free tier + Pro upgrade | Reuses old devices |
| QMK/VIA macropads | Keyboards/pads | USB HID; **Raw HID** (QMK) for host↔device messages; ESPHome for WiFi pads (see 1.3) | Open firmware | Open source **(unverified details)** | Per-key RGB layers, no vendor lock-in |
| Flipper-like controllers | Pocket multi-tool | USB/BLE HID | Open apps | **(unverified)** | Not tested; limited relevance |
| M5Stack StopWatch (target hardware) — https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3 | 1.75" 466×466 AMOLED round touch, 2 prog buttons + power, vibration motor, BMI270 IMU, ES8311 codec + MEMS mic, RX8130 RTC, 450 mAh, lanyard hole, rear magnet; ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 2.4 GHz Wi-Fi | — | Arduino/ESP-IDF/UiFlow | $45 (C152) | Round glanceable face; haptics; wearable |

### Developer notification apps on watches (CI/GitHub)
- No dedicated, popular "GitHub Actions on Apple Watch / Wear OS" app was found in this research (searched; only generic push actions such as https://github.com/marketplace/actions/api-alerts-github-action-notify and https://github.com/marketplace/actions/cloud-notify-send-push-notifications-to-your-devices turned up). Developers typically rely on phone push (ntfy/Pushover/Slack) mirrored to the watch. The Apple Watch agent approvers in 1.7 are the real analogues.

---

## 3. Software patterns

### 3.1 Agent lifecycle hooks (the data source)
- **GitHub Copilot CLI** hooks: https://docs.github.com/en/copilot/reference/hooks-reference and https://docs.github.com/en/copilot/concepts/customizations/hooks. Sources (loaded in order, combined): policy (`/etc/github-copilot/policy.d/*.json`, Windows ProgramData/Registry), repo `.github/hooks/*.json`, user `~/.copilot/hooks/*.json` (or `$COPILOT_HOME/hooks`), inline `settings.json`, plugin hooks; also reads `.claude/settings.json`. Event names found in the reference (camelCase runtime names with PascalCase aliases): sessionStart, sessionEnd, userPromptSubmitted, preToolUse, postToolUse, agentStop, subagentStop, permissionRequest, notification, errorOccurred, preCompact. Hooks can be `command` (per-OS overrides) or HTTP; hooks can programmatically approve/deny tool execution. Copilot CLI also has an SDK with hooks (https://docs.github.com/en/enterprise-cloud@latest/copilot/how-tos/copilot-sdk/features/hooks) and an experimental extension API used by Copilot Deck.
- **Claude Code**: https://code.claude.com/docs/en/hooks-guide ; events incl. SessionStart/End, UserPromptSubmit, PreToolUse, PermissionRequest (`hookSpecificOutput.decision.behavior: allow`), Notification (matchers `permission_prompt`, `idle_prompt`), Stop, SubagentStart/Stop, Elicitation; command and **HTTP hooks** supported.
- **Codex CLI**: three layers (https://codex.danielvaughan.com/2026/04/13/codex-cli-notification-pipeline-osc9-hooks-alerts/): TUI notifications (`[tui] notifications = ["agent-turn-complete","approval-requested"]`, `notification_method = osc9|bel|auto`), external `notify = [...]` program (as of April 2026 fires only for `agent-turn-complete`, tracked in openai/codex #11808, #14813), and newer `~/.codex/hooks.json` hooks (used by nimbus-notify).
- **Aider/Cursor/others**: no first-class hooks found; fall back to process/log watching (nimbus-notify does this for Vibe; watch-control polls tmux; edrethardo scans /proc).

### 3.2 Multi-session / terminal integrations
- agent-status (Rust, tmux status-right) — https://lib.rs/crates/agent-status : one JSON file per session, states notify/done/working/idle; agent-switcher popup.
- tmux-claude-status — https://github.com/farnots/tmux-claude-status : badges per window (working animated, "need input? 3m", waiting, error), jump key to next window needing input.
- ccmux — https://github.com/skzv/ccmux : TUI + `ccmuxd` daemon + HTTP API, one dashboard for Claude Code/Codex/Cursor across devices, mobile via Moshi.
- agent-mesh — https://github.com/danimoya/agent-mesh : tmux + SSH "no daemon, no broker" cross-host agent messaging.
- Terminal bell / OSC 9: Codex `osc9`; many terminals (Ghostty, iTerm2, Warp, WezTerm) convert to desktop notifications; coverage varies.

### 3.3 Mobile/remote approval relays
- ntfy / Pushover / Bark / Telegram (1.10); Tailscale for callbacks; Fly.io relay with outbound-only connections (1.6); Claude Remote Control (outbound-only, no open ports); CloudKit relay for Apple Watch (AgentDeck PoC, observation only).

---

## 4. Features worth co-opting

1. **Seven-ish canonical states** with fixed priority: Offline, Idle, Running, WaitingInput, AwaitingApproval, Done, Error (nimbus-notify nsn; claude-desktop-buddy). Keep "needs approval" distinct from "asked a question".
2. **Hold-the-response-open permission round trip** via PermissionRequest/preToolUse hook (agentsd, watch-control, Nick Liu blog). Add timeout → fall back to normal terminal prompt, not auto-deny.
3. **Click/double-tap to focus the session's window**, with precise targeting (`ITERM_SESSION_ID`, tmux pane, `TERM_PROGRAM`, process-tree walk) and **suppress alerts when that window is already focused** (steryereo, achowba, edrethardo, Copilot Deck).
4. **Bound approvals to the exact request** (request ID + subject shown); read-only when the subject can't be shown fully (AgentDeck contract). Show tool + path/command preview from transcript (claude-ntfy-hook).
5. **Risk-tiered gating**: auto-allow known-safe tools, read the agent's own allow/ask/deny rules, only escalate what matters (claude-ntfy-hook, Nick Liu options A–D); "Trust always / this time / deny" three-zone UI (Permitter).
6. **Device pairing/trust**: Hermes handshake (device key in NVS, hash-derived ID, pairing code approved on host); AgentDeck serial-provisioned WiFi token; BLE bonding for claude-desktop-buddy.
7. **Capability negotiation** in `hello` (display size, buttons, haptics, mic) so one daemon serves many devices (Hermes `caps`, AgentDeck device matrix).
8. **Snapshot-not-delta frames, "latest wins"**, with heartbeat/TTL and a "stale → offline" visual so stale tiles can't be acted on (nimbus-notify; copilot-stream-deck).
9. **Haptics/IMU as UX** (nobody uses a vibration motor for approvals yet; buddy uses shake/face-down; SmartKnob uses haptic resistance idea): distinct vibration patterns for approval vs done vs error; face-down/wrist-down to mute.
10. **Glanceable personality** without hurting legibility: breathing/pulse rings, "impatience" growth while a prompt waits (buddy), per-project accent colour (copilot-stream-deck), usage/quota gauge (AgentDeck, Copilot Deck).
11. **Simulator + browser flasher + OTA with rollback** (Hermes Gadget): hardware-free dev loop, Web Serial installer, auto-rollback.
12. **Device-initiated MCP/actions** (Xiaozhi, Hermes): later push-to-talk to agent; expose watch sensors as tools.
13. **Per-tool/per-event/per-project toggles** and quiet hours (achowba).

## 5. Common implementation patterns

- **Pipeline**: agent hooks (command or HTTP) → tiny shim (`led-report`, `agent-status set`) → Unix socket/localhost HTTP → daemon with per-session registry (JSON file or in-memory) → device protocol. Hooks are *appended* to existing arrays in `~/.claude/settings.json`; installer scripts must be idempotent and reversible (`hooks:install`, `uninstall`).
- **Transports to device**: USB-CDC serial JSON/binary (AgentDeck ESP32, nimbus-notify); WiFi WebSocket JSON (AgentDeck, Hermes, StackChan); BLE Nordic UART JSON (claude-desktop-buddy, nimbus-notify BLE); ESPHome native API (edrethardo); HTTP polling/POST (Permitter); cloud relay with outbound-only WSS (claude-code-remote, Remote Control).
- **Discovery**: mDNS, port scan, QR code, manual URL, serial handshake. **Auth**: bearer/session token, token provisioned over USB, per-device key + pairing code, Ed25519 (AgentDeck gateway).
- **Session identity**: hook payload `session_id` + `cwd`; fallback to hash of cwd; PID scans for sessions that predate hook install.
- **Focus**: AppleScript/Accessibility on macOS (terminal tab via `ITERM_SESSION_ID`), `tmux select-window`, URL handlers (`ghapp://`), VS Code URIs; Windows/Linux focus is the weakest area in existing tools (agentsd is macOS-only; Copilot Deck macOS-only).
- **Message shape**: full snapshot per frame with seq, plus separate event stream for approvals with request ID, subject, options, expiry.
- **Distribution**: PlatformIO / Arduino firmware, release binaries + browser installer, npm/pip CLI for daemon, systemd user service / launchd agent.

## 6. Gaps / differentiation opportunities

1. **Copilot CLI first-class + cross-agent**: AgentDeck lists Claude Code/Codex/OpenCode; Copilot Deck covers only Copilot CLI on macOS+Stream Deck. A single daemon that normalises Copilot CLI, Claude Code, Codex (and Cursor/Aider via adapters) into one state model is open space.
2. **Multi-PC routing**: "which PC" is rarely first-class. AgentDeck remote attach is experimental; nimbus-notify, edrethardo and Permitter assume one host. Differentiator: one watch, N daemons, host badge per session, tap focuses the *right machine* (and a KVM/remote-desktop/SSH hand-off if the PC is remote).
3. **Platform independence**: most host tools are macOS-only (agentsd, Copilot Deck, steryereo, achowba). Cross-platform focus (Windows `SetForegroundWindow`/UIA, Linux X11/Wayland/`tmux`) would differentiate; Wayland focus-stealing restrictions are a risk.
4. **Wearable + haptic + round AMOLED**: no found project uses vibration for approval/done patterns or a 466×466 round face; most are keys, knobs, or tiny 135×240 LCDs. Apple Watch solutions need iPhone + cloud paths; a self-hosted LAN watch avoids that.
5. **Battery-friendly offline operation**: StopWatch with 450 mAh needs deep sleep + wake-on-button/IMU + batched Wi-Fi; most existing devices are USB-powered. Design for "wake on event via light-sleep Wi-Fi" or BLE relay. (Hermes marks StopWatch battery reporting as experimental.)
6. **Safety-focused approvals** with request binding and risk tiers on a tiny screen; most projects allow blind approve-all.
7. **StopWatch compatibility with existing ecosystems**: reuse Hermes Gadget board profile for StopWatch (pin map, display/touch/audio drivers) rather than re-deriving hardware init. Could also speak AgentDeck's device protocol (`esp32-client-contract.md`) for instant ecosystem compatibility.
8. **Voice quick replies** to a coding agent from a watch are untested in the open-source field (Hermes does voice but to Hermes Agent, not Copilot/Claude).

## 7. Pitfalls users complain about

- **Notification hooks unreliable in non-terminal surfaces**: Claude Code `Notification` hook (permission_prompt/idle_prompt) does not fire in the VS Code native panel or Desktop app — https://github.com/anthropics/claude-code/issues/83577 (closed as duplicate of #79881), https://github.com/anthropics/claude-code/issues/81788 ("Not planned"). Stop/PreToolUse still fire. Mitigate with multiple signals (PermissionRequest, Stop, idle timers).
- **Codex external `notify` fires only on turn-complete**, not approval requests (openai/codex #11808, #14813, per the Codex Knowledge Base page above).
- **Rules silently override hook decisions**: Claude Code writes `ask` rules into `settings.local.json` on first denial, and `ask` overrides a hook's "allow", so a device approval lands but the terminal still prompts (https://nick-liu.com/posts/permission-round-trip/). Hook timeout (600 s) bounds how long you can hold the HTTP open; Stream Deck "hold" is 120 s in agentsd.
- **Four-process pipelines are hard to debug**: need per-hop logs (hook, daemon, device, WS) and a diagnostics command (AgentDeck `agentdeck diag agents`).
- **Hooks only apply to new sessions**: "already-open Claude sessions pick up new hooks only after a restart" (tmux-claude-status); edrethardo shows a "blue" state for pre-hook sessions.
- **Stale/ghost sessions** (killed with `kill -9`, unattended `ScheduleWakeup`/cron sessions pinning "waiting"): need TTL, heartbeat, process liveness sweeps (nimbus-notify, tmux-claude-status ≤5 s sweep).
- **Too many notifications**: users want filtering of safe tools and suppression when the window is already focused (claude-ntfy-hook, steryereo, achowba).
- **Security of remote approval**: relay tokens equal shell access (claude-code-remote warns); `bypassPermissions` runners; Claude Code's destructive `rm -rf` incidents show approvals matter (https://www.truefoundry.com/blog/claude-code-dangerously-skip-permissions). Never expose daemon ports without auth; avoid fake keep-alive traffic or auto-approvals (copilot-stream-deck's explicit non-goals).
- **Copilot runtime lifecycle**: idle runtimes are reaped (~10 min), extension bridges disappear, and no supported wake API exists (copilot-stream-deck integration.md); experimental/undocumented APIs can break (Logitech companion files).
- **Hardware/firmware friction**: USB-CDC corrupts esptool above 230400 baud on some boards and Wi-Fi join concurrent with display bring-up can brown out (AgentDeck devices.md); ESP32 ports stay "experimental" until verified on the exact hardware revision (Hermes); macOS asks for Bluetooth/Accessibility/Automation permissions repeatedly; BLE API in Claude is Developer-Mode-only and unsupported.
- **Platform lock-in**: macOS-only plugins, Stream Deck app dependency, Apple Watch needs iPhone + Apple developer account, closed Elgato firmware.
- **Tiny screens truncate context**: AgentDeck caps subject/option lengths; users must fall back to a larger screen for long approvals.

## 8. Source list (primary)

- https://github.com/puritysb/AgentDeck
- https://github.com/ristllin/nimbus-notify
- https://github.com/edrethardo/agent-deck
- https://github.com/anthropics/claude-desktop-buddy
- https://github.com/ericfriedman/permitter
- https://github.com/CVSRohit/claude-code-remote
- https://github.com/shobhit99/claude-watch (summary: https://hysenlabs.com/projects/shobhit99-claude-watch)
- https://github.com/CryptoPilot16/watch-control
- https://github.com/allinnb/hermes-gadget-sdk , https://github.com/Adolanium/hermes-gadget-sdk
- https://github.com/paultyng/agentsd
- https://marketplace.elgato.com/product/copilot-deck-fe397e6a-5052-4497-875e-741cabca39f2
- https://github.com/haflidif/copilot-stream-deck
- https://github.com/78/xiaozhi-esp32 , https://github.com/m5stack/StackChan
- https://github.com/scottbez1/smartknob
- https://github.com/skzv/ccmux , https://github.com/farnots/tmux-claude-status , https://lib.rs/crates/agent-status , https://github.com/danimoya/agent-mesh
- https://docs.github.com/en/copilot/reference/hooks-reference
- https://code.claude.com/docs/en/hooks-guide
- https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3

## 9. Limitations of this research

- Search tool was rate-limited mid-session; Kuando, Embrava, Ulanzi pricing, QMK Raw HID details, and Flipper-class controllers were not independently verified.
- Star counts and "last updated" values come from search snippets and may be stale.
- Claude Desktop Buddy `REFERENCE.md`, AgentDeck `esp32-client-contract.md`, and Hermes `docs/hardware.md#m5stack-stopwatch` were not read in full; fetch these next when designing the protocol and firmware.
- No evidence found of any existing Copilot CLI + ESP32/wearable project; absence in search results is not proof.
