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

## Repository map

| Path | What it is |
|---|---|
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

## Status

Early scaffold. See `docs/design/DESIGN.md` §13 for phasing.

## License

Dual-licensed under [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your option.
