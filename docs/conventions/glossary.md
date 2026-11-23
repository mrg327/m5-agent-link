# Glossary

The project's words, what each one means here, and how code spells it. Issue 20261122-db53
(phase 0). Use these words in docs, commits, issues and identifiers. When a doc and this page
disagree, the doc named in the last column defines the term; fix this page.

In the code column, `wire:` is the CDDL name ([protocol.md](protocol.md) §3 says how each language
spells it), `C:` is the firmware core or HAL, and `Rust:` is the daemon workspace. A dash means the
term has no identifier.

## Words that mean two things

Say which one you mean.

| Word | Meanings | Write |
|---|---|---|
| agent | (1) the coding agent whose sessions the watch shows; (2) an AI contributor working on this repo (`AGENT_NAME`) | "agent" for (1); "contributor" or the agent's name for (2) |
| attention / request / card | one thing the agent waits on you for | **request** in prose and in the daemon (`broker::Request`, `rid`); **attention** only for the wire message and the C struct (`attention_t`); **card** only for how ASK shows it |
| approval / permission | a request to run a tool | **approval** in wire and code (`att_kind_approval`); "permission request" only for Claude Code's hook (`PermissionRequest`); the UI shows a glyph |
| question | (1) `att_kind_question`, an agent asking; (2) session `q`, the closing question of a turn; (3) `qs`, Claude Code's multiple-choice questions | "question", "closing question", "Ask questions" |
| badge | (1) badge mode, showing the device off (deferred); (2) a count drawn on a disc or the ring | "badge mode"; "count badge" |
| delta | (1) a `session_upd` carrying only changed keys; (2) a patch firmware update | "session delta"; "delta update" |
| link / transport | the handshake-level connection vs the carrier under it | **link** = one device and one daemon after `welcome`; **transport** = USB, BLE or WebSocket |
| model | (1) the device's state store (`model.c`); (2) the AI model's name (`mdl`) | "model" for (1) in C; "AI model" for (2) |
| ring | (1) the session ring on screen; (2) the "Ring" UI proposal (UI-RING.md, superseded) | "ring"; "the Ring proposal" |
| screen / mode | a page of the watch UI | "screen" in docs, `MODE_*` in code, names in capitals (HOME, ASK) |
| session | an agent conversation the daemon tracks | never a BLE or security session: that is a link |
| sigil | (1) the glyph-first design system; (2) a project's generated mark | "Sigil" (capital) for (1); "sigil" for (2) |
| stop / interrupt / cancel | end the agent's running turn | "Stop" in the UI, `cmd_op_cancel` on the wire; `AG_INTERRUPT` is an older name |
| watch | (1) the StopWatch device; (2) the WATCH screen (shown as "Activity") | "watch" for (1); "WATCH" or "Activity" for (2) |

Avoid also: **Glance** (DESIGN.md's first screen, now HOME), **Deck** for HOME outside UI-V2, and
**FOCUS** as a UI word (UI-V3 §6: "Open on PC").

## Terms

| Term | Meaning | Code | Defined in |
|---|---|---|---|
| Ack (Dismiss) | Device clears an error or done request | wire `cmd_op_ack`, `action_ack`; C `LINK_OP_ACK`, `AG_DISMISS`; Rust `CmdOp::Ack` | PROTOCOL.md §5 |
| Activity | UI name of the WATCH screen | C `MODE_WATCH` | UI-V3.md §4.4 |
| Agent | The coding agent: Claude Code, Copilot CLI, Copilot cloud, pi, or a job | wire `agent`; C `agent_t`, `AGENT_*`; Rust `Agent` | DESIGN.md §4 |
| AGENT | Session menu: one session's actions | C `MODE_AGENT`, `ui_agent.c`, `AG_*` | UI-V3.md §4.2 |
| Agent graph | The turn's tool calls as nodes on lanes | wire `graph`, `cmd_op_graph`; C `graph.c`, `MODE_GRAPH`; Rust `agentlinkd/src/graph.rs` | GRAPH.md |
| `agentlink` | The CLI: `decode`, `bench`, `hooks`, `shell-init`, `ssh`, `trace`, `ota` | `daemon/crates/agentlink` | daemon/README.md |
| `agentlinkd` | The daemon on the PC | `daemon/crates/agentlinkd` | DESIGN.md §3 |
| Alert mode | full, quiet (haptics only) or silent (glow only) | wire `alert_mode`; C `alert_mode_t`, `ALERT_*`; Rust `AlertMode` | DESIGN.md §6 |
| Always allow | UI for granting a standing rule, after a 1.6 s hold | C `ASK_GRANT`, `NAV_GRANT_HOLD_MS` | STANDING-RULES.md |
| Ambient face | HOME dimmed after 30 s without input | C `ui_dimmed()`; power report `dim` | POWER.md |
| Answer window | How long a blocking hook holds the terminal prompt for the watch (default 60 s) | wire `attention.left`, `.win`; C `ui_countdown.c`; Rust `window.rs` | ANSWER-WINDOW.md |
| Approval | A request to run a tool; answered by approve or deny | wire `att_kind_approval`, `input_sub_approval`; C `ATT_APPROVAL`; Rust `AttKind::Approval` | PROTOCOL.md §4 |
| Approve | Commit an approval: a 1.2 s hold bound to the `rid`, never a tap | wire `cmd_op_approve`, `hold_ms`; C `NAV_HOLD_MS` | UI-V3.md §1 |
| Arc | A quantity drawn as an arc (countdown, gauge) | C `ui_arc_*` | GLYPHS.md §3.9 |
| ASK | Screen that answers a request: its card and actions | C `MODE_ASK`, `ui_ask.c`, `ask_action_t` | UI-V3.md §4.3 |
| Ask questions | Claude Code `AskUserQuestion` choices, answered all at once | wire `ask_q`, `ask_opt`, `attention.qs`, `cmd_op_answer`; C `ask_q_t`; Rust `AskQ` | LAST-MESSAGE.md §3 |
| Atlas | The interactive visual guide; runs the firmware core as WebAssembly | `index.html`, `atlas/`, `scripts/atlas`; facts in `atlas/data/facts.json` | atlas/CONTRACT.md |
| Attention | The wire message that announces a request | wire `mt_attention` (6), `attention`; C `attention_t`; Rust `Attention` | PROTOCOL.md §4 |
| Audit log | Every grant and answer, as JSONL | `~/.agentlink/audit.jsonl` | README.md |
| Away events | What happened while the device was not linked, sent in the snapshot | wire `snapshot.away`, `away_event` | PROTOCOL.md §4 |
| Badge mode | Showing the device off; deferred | wire `feature_badge` | DESIGN.md §8 |
| Bench | Drives the real watch as a finger and records a frame timeline | `agentlink bench`; wire `dbg_touch`, `dbg_bench`, `dbg_frames`; C `bench.c`; Rust `agentlinkd/src/bench.rs` | BENCH.md |
| Board | One hardware target implementing the HAL | `firmware/boards/<name>/`; C `hal_board_info_t` | FIRMWARE.md §2 |
| Broker | Daemon part that holds a blocking hook until the watch or terminal answers | Rust `broker.rs`: `Broker`, `Request`, `Decision` | DESIGN.md §6a |
| Burst | A result (approved, sent) as one hero glyph, then the screen moves on | C `ui_burst` | GLYPHS.md §3.7 |
| Caps | What the device can do, sent in `hello` | wire `caps`, `feature_*`; Rust `Caps`, `Feature` | PROTOCOL.md §3 |
| Card | How ASK shows one request: text, glyph, actions | C `ui_ask.c` | UI-V2.md §4.3 |
| CDDL | The schema language of the protocol (RFC 8610) | `protocol/agentlink.cddl` | protocol.md |
| Clear | Wire message: a request is gone, with the reason | wire `mt_clear`, `clear_why`; Rust `Clear`, `ClearWhy` | PROTOCOL.md §4 |
| Closing question | The question an agent ended its turn with, and its replies | wire `session.q`, `.rp` | PROTOCOL.md §4 |
| Command | A device → daemon action | wire `cmd`, `cmd_op`; C `link_op_t`, `LINK_OP_*`; Rust `Cmd`, `CmdOp` | PROTOCOL.md §5 |
| Comet | The light that sweeps a working session's capsule | C `RING_WORK`, `ring_comet_span()` | UI-V3.md §3.1 |
| Core | Portable app logic and UI, built for device, simulator and WebAssembly | `firmware/components/core` | FIRMWARE.md §2 |
| Daemon | `agentlinkd`; the UI calls it "your computer" | — | DESIGN.md §3 |
| Deck | UI v2's name, and its HOME screen | — | UI-V2.md |
| Deep sleep | Link off, only wake sources powered | C `HAL_POWER_DEEP`; power report `deep` | FIRMWARE.md §4 |
| Delta update | Firmware update sent as a patch against the running image | wire `fw_patch`; Rust crate `agentlink-delta` | FIRMWARE.md §9 |
| Deny | Refuse an approval: a 0.8 s hold or the strict long swipe | wire `cmd_op_deny`; C `NAV_DENY_HOLD_MS` | UI-V3.md §1 |
| Detail | A request's or last message's full text, in chunks of at most 1024 B | wire `mt_detail`, `cmd_op_detail`; C `detail.c`; Rust `Detail` | ANSWER-WINDOW.md |
| Device | Anything that speaks the protocol to a daemon: the watch, the simulator, a future desk unit | wire `device_info`, `device_class` | DESIGN.md §3.1 |
| Disc | An action: a glyph in a filled circle | C `ui_disc_*` | GLYPHS.md §3.4 |
| Done | A session finished its turn; also the request kind for it | wire `sess_state_done`, `att_kind_done`; C `SESS_DONE` | PROTOCOL.md §4 |
| Done summary | What a turn did: files, lines, tests, time | wire `session` keys 30-34; C `done_sum_t`, `done.c`; Rust `turn::DoneSummary` | DONE-SUMMARY.md |
| Dots | Elapsed time, one dot a minute, every tenth larger | C `ui_dots_*`, `ui_tally_*` | GLYPHS.md §3.3 |
| Envelope | Every message: `{t, id, re, body}` | wire `envelope`; C `struct envelope_r`; Rust `Envelope`, `Message` | PROTOCOL.md §2 |
| Feature | One capability flag in `caps.feats` | wire `feature_*`; Rust `Feature` | PROTOCOL.md §3 |
| Firmware update (OTA) | The daemon writes a signed image to the spare app slot | wire `fw_begin`, `fw_chunk`, `fw_ack`; C `ota.c`; Rust `ota.rs`; `agentlink ota` | FIRMWARE.md §9 |
| Focus (Open on PC) | Bring the session's terminal to the front | wire `cmd_op_focus`, `action_focus`; C `LINK_OP_FOCUS`, `AG_FOCUS` | DESIGN.md §10 |
| Focus driver | Daemon code per terminal or OS that focuses, scrolls and types | Rust `agentlinkd/src/focus/` (kitty, tmux, macOS Terminal, Windows Terminal) | DESIGN.md §10 |
| Gauges | Plan usage (5 h, weekly) and per-session context, model, mode, PR | wire `usage`, `session.ctx`…`prs`; C `gauges.c`; Rust `usage.rs` | GAUGES.md |
| Gesture | A touch, button or motion event after recognition | C `gesture_kind_t`, `G_*`; wire `input_event` (debug injection) | UI-V2.md §3 |
| Glow | A brief dim ring lit for a request while the display is off; the first touch only reveals | C `power.h` | DESIGN.md §6 |
| Glyph | A symbol that replaces a word; Phosphor icons | C `ICON_*`, `ui_glyph_*`, `theme_glyph()` | GLYPHS.md §3.2 |
| Halo | Codename of UI v3 | — | UI-V3.md |
| HAL | The hardware contract every board, the simulator and the Atlas implement | C `hal.h`, `hal_*`, `HAL_*` | FIRMWARE.md §2 |
| Hand back | The device's Open on PC releases the hook so the terminal asks at once | wire `clear_why_handed_back`; C `BACK_HANDED` | ANSWER-WINDOW.md |
| Handshake | `hello`, `welcome`, then `snapshot`; "welcomed" when done | wire `mt_hello`, `mt_welcome`; C `link_welcomed()` | PROTOCOL.md §3 |
| Hold | Press and hold to commit what cannot be undone; a ring fills | wire `cmd.hold_ms`; C `NAV_HOLD_*`, `ui_disc_set_hold()` | UI-V3.md §1 |
| HOME | The main screen: the selected session's state | C `MODE_HOME`, `ui_home.c` | GLYPHS.md §4 |
| Hook | Command an agent runs at its events; talks JSON to the daemon socket | Rust crate `agentlink-hook`; `agentlink hooks` | DESIGN.md §4 |
| Host | A machine running agents; index 0 is the daemon's own PC | wire `welcome.host`, `session.host`, `.hn`; Rust `HostCtx` | REMOTE.md |
| Hush | No buzz or glow: you are looking at that terminal | wire `attention.hush`, `session.hush` | PRESENCE.md |
| Ids | `sid` session, `rid` request, `aid` recording, `kid` keyboard opening, `uid` device | wire keys of those names | PROTOCOL.md |
| Issue | A tracker entry, ID `YYYYMMDD-xxxx` | `issues/`, `scripts/issue` | issues/README.md |
| Job | A terminal command shown as a session | wire `agent_shell`; C `AGENT_SHELL`; Rust `Agent::Shell`, `jobs.rs` | JOBS.md |
| KEYS | Swipe keyboard: the watch sends key geometry and finger paths, the daemon decodes and types | wire `kb_start`, `kb_stroke`, `kb_cands`, `cmd_op_kb_*`; C `MODE_KEYS`, `keys.c`; Rust crate `agentlink-keys` | PROTOCOL.md §6a |
| Key hint | What button A or B does, as a glyph at the button | C `ui_key_hint_*` | GLYPHS.md §3.6 |
| LABS | Experiments menu, a swipe up on HOME | C `MODE_LABS`, `ui_labs.c` | EXPERIMENTS.md |
| Lane | A row of the agent graph: 0 the session's agent, 1-7 sub-agents | wire `graph.la`; C `GL_*` | GRAPH.md |
| Last message | The agent's last message, read on READ | wire `session.lm`, `.ll`, `.lp`; Rust `lastmsg.rs` | LAST-MESSAGE.md |
| Link | One device and one daemon, handshake done, over one transport | C `link.c`, `link_*`; Rust trait `Link` | PROTOCOL.md §3 |
| Model (C) | The device's store of sessions and requests | C `model.c`, `model_*` | FIRMWARE.md §2 |
| Multi-link | One watch linked to two machines at once | C `HAL_LINK_BLE2` | MULTI-LINK.md |
| Nav | The state machine from gestures to screens and actions | C `nav.c`, `nav_*`, `NAV_*` | UI-V3.md §5 |
| Notify when done | Buzz when this session finishes | wire `cmd_op_notify_done`, `session.ntf`; C `AG_NOTIFY` | PROTOCOL.md §4 |
| Orbit | A menu: discs on a circle round a subject | C `ui_orbit_*` | GLYPHS.md §3.5 |
| Peer minor | The proto minor the other side sent; gates what is sent to it | C `model_peer_minor()`; Rust `peer_minor`, `*_MINOR` | protocol.md §5 |
| Permission mode | Claude Code's mode: default, plan, accept edits, auto, don't ask, bypass | wire `perm_mode`; C `PM_*`; Rust `PermMode` | GAUGES.md |
| pi | The pi coding agent | wire `agent_pi`; `daemon/integrations/pi` | PI.md |
| Pip | A dot on an unselected capsule: that session needs you | C `ui_pips_*` | UI-V3.md §3.1 |
| Power report | Battery and screen-time figures, device → daemon every 5 min | wire `mt_power`; C `power_report.c`; Rust `power.rs` | POWER.md |
| Presence | The daemon's reading of whether you are at the PC and in which terminal | Rust `presence.rs` | PRESENCE.md |
| Privacy | How much agent text reaches the device: meta, preview, full | wire `privacy`; C `PRIVACY_*`; Rust `Privacy` | PROTOCOL.md §3 |
| proto | Protocol version `[major, minor]`, 0.24 today | Rust `PROTO`; C `hello_major_l_minor` | protocol.md §5 |
| Queued message | A reply kept until the session can take input | wire `session.qd`, `cmd.q`, `cmd_op_unqueue`; Rust `queue.rs` | PROTOCOL.md §6d |
| Quick reply | One of up to 6 user-set replies typed into the terminal | wire `welcome.replies`, `cmd_op_type`; `~/.agentlink/replies.txt` | PROTOCOL.md §3 |
| READ | Screen that shows the last message | C `MODE_READ`, `ui_read.c` | UI-V3.md §4.10 |
| Remote session | A session on another machine, reached over SSH | `agentlink ssh`; Rust `remote.rs`; wire `session.host` ≥ 1 | REMOTE.md |
| Report | Each screen's meaning in words, for logs and tests: the legend for every glyph | C `ui_report()`, `ui_report_t`; wire `dbg_ui_state` | GLYPHS.md §1 |
| Request | One thing the agent waits on you for, named by `rid` | Rust `broker::Request`; C `attention_t` | PROTOCOL.md §4 |
| Rerun | Run a finished job's command again | wire `cmd_op_rerun`; C `AG_RERUN` | JOBS.md |
| Result | The daemon's answer to a command: ok or an error | wire `mt_result`, `result_err`; Rust `CmdResult`, `ResultErr` | PROTOCOL.md §5 |
| Ring | One capsule per session round the screen edge | C `ui_ring.c`, `RING_*` | UI-V3.md §3.1 |
| SCROLL | The watch scrolls the terminal's scrollback | wire `cmd_op_scroll`, `scroll_arg`; C `MODE_SCROLL`; Rust `focus/scroll.rs` | UI-V2.md §6 |
| Scrub | A finger moving round the rim to move a selection | C `G_SCRUB*`, `NAV_SCRUB` | UI-V2.md §3 |
| Session | One agent conversation or job, named by `sid` | wire `session`, `mt_session_upd`; C `session_t`; Rust `Session`, `Registry` | PROTOCOL.md §4 |
| Session state | start, work, input, error, done, idle, ended | wire `sess_state`; C `SESS_*`; Rust `SessState` | DESIGN.md §4 |
| Sigil | A project's mark, generated from its name's hash | C `ui_sigil_*` | GLYPHS.md §3.1 |
| Sign | An error as one or two glyphs ("computer, refused") | C `ui_sign_*` | GLYPHS.md §3.7 |
| Simulator | The firmware core on SDL2, linked to the daemon over WebSocket; scripts and ctest | `simulator/`, `sim_*.c`; wire `device_class_sim` | simulator/README.md |
| Snapshot | All sessions, sent after `welcome` | wire `mt_snapshot` | PROTOCOL.md §3 |
| Snooze | Hide a session for some minutes | wire `cmd_op_snooze`, `session.snz`; C `AG_SNOOZE` | PROTOCOL.md §5 |
| Standby | Display off, light sleep, link kept | C `HAL_POWER_STANDBY` | FIRMWARE.md §4 |
| Standing rule | A Claude Code permission rule granted from the watch | wire `attention.rule`, `.scope`, `.wide`, `cmd.rh`; Rust `standing.rs` | STANDING-RULES.md |
| STATUS | Control centre: quiet, sounds, privacy, sessions, firmware | C `MODE_STATUS`, `ui_status.c`, `ST_*` | UI-V3.md §4.6 |
| Step | One tool call within a turn | wire `session.n`, `.ok`; C `steps.c`; Rust `registry::Step` | PROTOCOL.md §4 |
| Stop | End the agent's running turn (Esc into its terminal) | wire `cmd_op_cancel`; C `LINK_OP_CANCEL`, `AG_INTERRUPT` | UI-V3.md §6 |
| Task script | A `bench/tasks/*.txt` script the bench plays | `bench/tasks/` | BENCH.md |
| Toast | A hint, shown only after a wrong input | C `ui_toast()` | UI-V3.md §1 |
| Trace | Every message and agent event, as JSONL | `~/.agentlink/trace`; Rust `trace.rs`; `agentlink trace` | DESIGN.md §0 |
| Trackpad | The watch moves and clicks the PC's pointer | wire `cmd_op_pointer`, `pointer_arg`; C `MODE_TRACKPAD`; Rust `pointer.rs` | EXPERIMENTS.md |
| Transport | What carries frames: USB CDC, BLE GATT, WebSocket | C `hal_link_kind_t`, `HAL_LINK_*`; Rust trait `Transport` | PROTOCOL.md §1 |
| Turn | The agent's work from a prompt to its stop | Rust `turn.rs`, `registry::Turn` | DONE-SUMMARY.md |
| UPDATING | Screen shown during a firmware update | C `ui_ota.c` | UI-V3.md §4.9 |
| Vector | A shared test case: symbolic JSON and its CBOR hex | `protocol/vectors/*.json` | protocol/README.md |
| VOICE | Dictation: the watch records, the daemon transcribes and types | wire `audio_*`, `transcript`, `cmd_op_dictate`; C `MODE_VOICE`, `voice.c`; Rust `voice.rs` | PROTOCOL.md §6 |
| Watch | The M5Stack StopWatch, the first device | wire board `"m5stopwatch"`, `device_class_watch` | docs/StopWatch.md |
| WATCH | Follow one session's steps live; shown as Activity | C `MODE_WATCH`, `ui_watch.c`, `AG_WATCH` | UI-V3.md §4.4 |
| Wheel | A three-row list or button picker | C `ui_wheel`, `WHEEL_STYLE_*` | UI-V3.md §3.2 |
| zcbor | Generates the C CBOR codec from the CDDL | `firmware/components/agentlink_proto/regen.sh` | protocol/README.md |
