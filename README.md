# m5-agent-link

A wearable control surface for AI coding agents. A small device (initially the
**M5Stack StopWatch**) shows the state of your GitHub Copilot CLI, Copilot cloud
agent, and Claude Code sessions. When an agent finishes or needs input, it buzzes,
and a double-tap brings that agent's terminal to the front on your PC.

```
 agents ──hooks──► agentlinkd (PC, Rust) ──BLE / USB / WebSocket──► device firmware (ESP32 + LVGL)
                        │                                                │
                   ~/.agentlink/trace (JSONL)                    `#` debug lines over USB
```

**New here? Open the Atlas.** It is an interactive, visual guide to how the watch works:
what it shows, how data travels, how gestures are read and how haptics feel. The watch on
the page runs the real firmware, compiled to WebAssembly.

```sh
scripts/atlas --open        # serves index.html at http://localhost:8700/
```

## Repository map

| Path | What it is |
|---|---|
| `index.html`, `atlas/` | **The Atlas**, the living visual guide (facts generated from source; see `atlas/CONTRACT.md`) |
| `docs/design/` | **Start here.** `DESIGN.md`, `PROTOCOL.md`, `FIRMWARE.md` |
| `docs/research/` | Background research: similar products, use cases, host integration, UI |
| `docs/` (rest) | Vendor documentation for the StopWatch (`uv run scripts/fetch_docs.py`) |
| `protocol/` | Normative CDDL schema plus shared test vectors |
| `daemon/` | Rust workspace: `agentlinkd`, `agentlink-hook`, `agentlink` CLI |
| `firmware/` | ESP-IDF project: portable `core` + `hal` contract + `boards/<name>` |
| `simulator/` | The same firmware core on SDL2, linked to the daemon over WebSocket |

## Principles

1. **Auditable.** Every message and every agent event is traced, and every screen
   can be inspected or driven from the CLI.
2. **Readable.** Small modules with one concern each, and contracts written before code.
3. **Device-agnostic.** The daemon sends semantic data, never pixels, and devices
   declare their capabilities.

## Setup (user)

Run the daemon (`cargo run -p agentlinkd` in `daemon/`; see `daemon/README.md`). It serves USB, BLE and a WebSocket simulator link at once.

**macOS permissions**
- **Bluetooth:** allow the terminal (or the `agentlinkd` binary) under System Settings › Privacy & Security › Bluetooth, or BLE scanning finds nothing.
- **Automation:** to focus Terminal.app, allow your terminal/`agentlinkd` to control Terminal under System Settings › Privacy & Security › Automation.

**kitty:** add this to `kitty.conf`, then restart kitty (a config reload is not enough):
```
allow_remote_control socket-only
listen_on unix:/tmp/kitty
```

**Daemon environment**

| Variable | Effect |
|---|---|
| `AGENTLINK_USB=0` / `AGENTLINK_BLE=0` | Disable that transport (`0`, `false` or `off`) |
| `AGENTLINK_HOME` | State directory (default `~/.agentlink`: hook socket, trace, audit log) |
| `AGENTLINK_WS_ADDR` | WebSocket listen address (default `127.0.0.1:7878`) |
| `AGENTLINK_USB_PORT` | Serial port to use instead of auto-discovery (VID 0x303A, PID 0x1001) |
| `AGENTLINK_BLE_NAME` | Connect only to the device advertising this name (`AgentLink-XXXX`) |
| `AGENTLINK_BLE_WRITE_LEN` | BLE write size in bytes (default 20) |
| `AGENTLINK_PIPE` | Windows only: hook named-pipe name override |

Hook IPC uses `$AGENTLINK_HOME/hook.sock` on Unix. On Windows it is the named pipe `\\.\pipe\agentlink-<USERNAME>-<fnv64 of the socket path>`.

Remote sessions (Copilot on a Windows machine reached over SSH, watch on this machine) are designed but not built yet: [docs/design/REMOTE.md](docs/design/REMOTE.md).

## Status

Early scaffold. See `docs/design/DESIGN.md` §13 for phasing.

## License

Dual-licensed under [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your option.
