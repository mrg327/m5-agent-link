# Design docs

One doc per feature or subsystem, written before or with the code and kept true after it ships
([docs.md](../conventions/docs.md) §2). A new one starts from
[templates/design-doc.md](../conventions/templates/design-doc.md) and is named `<NAME>.md` in upper case
with hyphens (structure.md S-NEW-07). Background research is in `docs/research/`.

Status is quoted from the top of each doc as of 2026-11-22; "–" means the doc has no status line.
Many "built on branch …" docs have since been merged into `main`.

## System

| Doc | What it covers | Status |
|---|---|---|
| [DESIGN.md](DESIGN.md) | The overall design: requirements, architecture, decisions | Under discussion (working draft) |
| [FIRMWARE.md](FIRMWARE.md) | Firmware architecture, the HAL, boards, OTA (§9), StopWatch gotchas (§12) | [P] draft for discussion |
| [PROTOCOL.md](PROTOCOL.md) | The wire protocol: envelope, messages, versions, AEAD | [D] v0 decisions: integer CBOR keys, app-layer AEAD, zcbor C codegen |
| [MULTI-LINK.md](MULTI-LINK.md) | One watch, several machines | Phase 1 built on branch `multilink` |
| [REMOTE.md](REMOTE.md) | Remote sessions over SSH | Built; a full interactive Copilot session still pending |
| [PI.md](PI.md) | The pi coding agent extension | – |
| [BENCH.md](BENCH.md) | Drive the real watch like a finger and measure what the user feels | – |
| [POWER.md](POWER.md) | Power report: where the battery goes | – |

## Watch UI

| Doc | What it covers | Status |
|---|---|---|
| [UI-RING.md](UI-RING.md) | "Ring": select a session, act with one gesture | Implemented; superseded by UI-V2 |
| [UI-V2.md](UI-V2.md) | "Deck": switch context, pick from a list, answer from the watch | Decided; look superseded by UI-V3, interaction and safety rules stand |
| [UI-PROFILE.md](UI-PROFILE.md) | Audit of the watch as it stood before v3 (2026-11-10) | – |
| [UI-V3.md](UI-V3.md) | "Halo": the Apple-style overhaul | Implemented in the simulator on branch `ui-v3` |
| [GLYPHS.md](GLYPHS.md) | "Sigil": the glyph-first design system | Being built on branch `glyphs`; replaces UI-V3's look |
| [glyphs/](glyphs/) | Sigil screens as built: [AGENT-READ](glyphs/AGENT-READ.md), [ASK](glyphs/ASK.md), [STATUS](glyphs/STATUS.md), [WATCH-GAUGES-DONE](glyphs/WATCH-GAUGES-DONE.md) | ASK: built in the simulator |
| [UX-PROFILE.md](UX-PROFILE.md) | The watch as the user meets it: speed, content, interruptions (2026-11-22) | – |
| [MOTION.md](MOTION.md) | Raise to wake, face-down silence, orientation, palm cover, tones | – |
| [EXPERIMENTS.md](EXPERIMENTS.md) | LABS and the Trackpad | – |

## Features

| Doc | What it covers | Status |
|---|---|---|
| [JOBS.md](JOBS.md) | Terminal commands on the watch | Built on branch `jobs`; protocol 0.9 |
| [GAUGES.md](GAUGES.md) | Plan usage and context on the watch | Built on branch `next`; protocol 0.10 |
| [ANSWER-WINDOW.md](ANSWER-WINDOW.md) | More time to answer, and all of a long request | Built on branch `next`; protocol 0.11 |
| [PRESENCE.md](PRESENCE.md) | Buzz when you are away, stay awake while work runs | Built on branch `next`; protocol 0.12 |
| [STANDING-RULES.md](STANDING-RULES.md) | "Always allow this" from the watch | Built on branch `next`; protocol 0.13 |
| [DONE-SUMMARY.md](DONE-SUMMARY.md) | What the turn did, at a glance | Built on branch `done-summary`; protocol 0.14 |
| [LAST-MESSAGE.md](LAST-MESSAGE.md) | Read what the agent said, answer what it asks | Built on branch `response`; protocol 0.18 |
| [GRAPH.md](GRAPH.md) | The agent graph: tool calls as glyphs on lanes (protocol 0.22) | – |

## Tools

| Doc | What it covers | Status |
|---|---|---|
| [BOARD.md](BOARD.md) | The design board ([tools/board](../../tools/board/README.md)) | Approved 2026-11-09; B1 and B2 built, B3 next |
