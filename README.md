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

**Terminal jobs:** to see builds, tests and other long commands on the watch (run time, an estimate, how
they ended; [docs/design/JOBS.md](docs/design/JOBS.md)), add one line to `~/.zshrc` or `~/.bashrc`:
```sh
eval "$(agentlink shell-init zsh)"   # or: bash
```

**Usage and context gauges:** to see the plan's 5-hour and weekly usage, each session's context fill, model,
permission mode and pull request ([docs/design/GAUGES.md](docs/design/GAUGES.md)), route Claude Code's status
line through the watch. `agentlink hooks statusline` prints the `statusLine` block for `~/.claude/settings.json`;
it keeps your own status-line command, if you have one.

**Answer window:** a permission request waits for the watch for 60 s by default before the terminal asks as
usual; the watch shows the time left as a thin ring and fetches the full text of a long command
([docs/design/ANSWER-WINDOW.md](docs/design/ANSWER-WINDOW.md)). Set it with `AGENTLINK_ANSWER_WINDOW_S` or one
number of seconds in `~/.agentlink/answer_window` (5-300; 0 = never hold), and give the two blocking hooks a
Claude Code `timeout` above it (`agentlink hooks` prints 330); the daemon shortens the window to fit a smaller one.

**Always allow:** when Claude Code suggests a rule for a permission request (`Bash(npm test:*)`), the watch offers
`Always allow` after Deny; it shows the rule exactly as it will be saved and where (this session, or this project's
`.claude/settings.local.json`) before a 1.6 s hold, never a whole-tool rule or one for all projects
([docs/design/STANDING-RULES.md](docs/design/STANDING-RULES.md)). Each grant is in `~/.agentlink/audit.jsonl`;
remove a rule with `/permissions` in Claude Code.

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
| `AGENTLINK_REMOTE` | Hook on a remote machine: `tcp:127.0.0.1:<port>`, the loopback end of an SSH tunnel to the daemon (REMOTE.md) |
| `AGENTLINK_SSH` | ssh binary used by `agentlink ssh` (default `ssh` on PATH) |
| `AGENTLINK_ANSWER_WINDOW_S` | Seconds a blocking hook holds the terminal prompt for the watch (default 60, 5-300, 0 = never); `~/.agentlink/answer_window` when unset |

Hook IPC uses `$AGENTLINK_HOME/hook.sock` on Unix. On Windows it is the named pipe `\\.\pipe\agentlink-<USERNAME>-<fnv64 of the socket path>`.

Remote sessions: Copilot can run on a Windows machine reached over SSH while the watch stays on this machine. Run `agentlink remote init` once, then `agentlink ssh <win-host>`. Setup and security: [docs/design/REMOTE.md](docs/design/REMOTE.md).

## Status

Early scaffold. See `docs/design/DESIGN.md` §13 for phasing.

## License

Dual-licensed under [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your option.
