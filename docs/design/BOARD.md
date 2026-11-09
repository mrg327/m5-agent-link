# Design board: plan

Status: draft for review, 2026-11-09. Nothing below is built yet except the channel proof of concept in `tools/board/` (commit `9ce8617`).

## Purpose

The design board is where the watch UI's current state and the decisions about it live. Chat stays the place to frame work and argue trade-offs; the board holds what was built, what's being chosen and what was decided. On the board you can:

- See every watch screen in every state, at true watch scale, from the current build.
- Point at a pixel and say what is wrong, then get 2–3 rendered fixes to choose from.
- Choose, and have the choice recorded together with the reason and the runner-up.
- See at a glance what the session is doing, and whether your input arrived.

## What the research says to keep

- **Side by side.** Compare variants next to each other, never one at a time. Label each variant by the one or two things it changes ("ring gap 6 / 10 / 14 px").
- **Comments anchor to a stable handle:** screen state id + build SHA + x/y on the 466 px canvas, never just "the button on the left".
- **Batch, then send.** Comments collect as drafts and leave together in one push, so the agent doesn't change direction halfway.
- **Picks write records,** not prompts. A pick is a JSON row the agent reads, not a sentence it has to interpret.
- **No stale boards.** Every image carries its SHA, and an image from an older build than HEAD is marked stale. Images are labelled `sim` or `device`, because the simulator is not the watch.
- **At most 3 open questions,** each with a preselected recommendation, plus Defer and Something else.
- **Visible delivery.** Every comment shows sent / seen / answered, so dropped input can't hide.
- **Board data in the repo,** so the board survives a tool change.

## Architecture

The architecture is the proof of concept, grown. There is still one process, `tools/board/board.mjs`, started by the Claude Code session as a channel server.

```
 phone / desktop browser ──HTTP+SSE──▶ board.mjs ──channel push──▶ Claude Code session
        ▲                               │  ▲                                │
        └──────── live updates ◀────────┘  └── fs.watch(design/board/) ◀────┘ edits files, calls tools
```

- **State is files.** `design/board/` in the repo holds `decisions.json`, `questions.json`, `variants/<id>.json` and `comments.jsonl`. The session updates the board by editing these files; the server watches them and pushes changes to the page. The data then stays reviewable in git, and needs no special tools.
- **Screenshots are generated.** `tools/board/render.sh` runs simulator scripts listed in `design/board/states.json` and writes `build/board/<sha>/<state>.png`. These images are git-ignored and rebuilt from any commit. The simulator already supports `shot FILE.png` in headless mode.
- **Channel events** (page → session): `comment-batch`, `pick`, `answer`, `render`, and the existing `ping`, `say` and `agent`. Each carries ids, never free-form instructions, except a comment's own text.
- **Tools** (session → page): `board_reply(id, text)`, which exists already, and `board_status(text)` for the "what I'm doing now" strip. Everything else goes through files.
- **Reach:** `127.0.0.1` plus the Tailscale address only. Comment text is treated as data: the session never runs commands or flashes the watch because a comment asked.

## Views

1. **Gallery** (home). One card per state (HOME one, busy, mixed, ambient; AGENT; ASK; SCROLL; STATUS …), drawn round at 1:1 with the state id, SHA and `sim`/`device` tag. A stale card is dimmed with a "rebuild" button.
2. **Compare.** Pick a state to see its variants in a row with the axis label. "Choose" writes a decision and sends a `pick`.
3. **Comment.** Tap a card to pin a note on the image, and repeat across cards. A tray shows the drafts and one Send button.
4. **Decisions.** The 26 decisions and the safety invariants from `UI-V2.md`, one line each with a thumbnail, the runner-up, the reason and active/superseded status. The invariants are pinned at the top and marked "never broken by a reskin".
5. **Questions.** The 0–3 open questions, answered with one tap.

The page stays one column at phone width.

## Phases

| Phase | Delivers | Proven when |
|---|---|---|
| B1 Gallery | `states.json`, `render.sh`, gallery view, SHA/stale stamps | Every UI-V2 state renders from HEAD, and a firmware change marks cards stale |
| B2 Comment | Pins, draft tray, batch send, sent/seen/answered | A batch of 3 pins on 2 cards arrives as one channel event; replies land on the right pins |
| B3 Decisions | `decisions.json` seeded from UI-V2.md, decisions view, questions view | The 26 decisions and invariants show; answering a question writes a record |
| B4 Variants loop | A pin can ask for fixes: Fable renders 2–3 variants (sim scripts with patched theme values, not committed), Compare view, pick → decision | A real complaint becomes 3 rendered options, and a pick, without leaving the board |
| B5 Later | Device-photo upload next to sim shots; a playable watch in the browser (Atlas `watch.wasm`, needs Emscripten); a permission relay to approve tool use from the board | — |

B1–B3 are each about a day of agent work. B4 is the payoff and the biggest.

## Open questions

1. **Where board data lives.** Recommended: `design/board/` on `ui-v2`, with decisions and comments committed and screenshots git-ignored. Alternative: outside the repo (`~/.agentlink/board/`).
2. **Which session hosts the board.** Recommended: a dedicated "board session" started by `scripts/board`, which relaunches with `--resume` and restarts daily. Its context stays small, and it delegates heavy work (renders, Fable passes) to background agents. Alternative: this working session.
3. **Seed the decision log from UI-V2.md now (B3 before B2)?** Recommended: no. B1 → B2 → B3 gives a usable feedback loop first.
