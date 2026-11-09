# Design board

A local page that talks to a running Claude Code session with no claude.ai artifact in between. Plan: `docs/design/BOARD.md`.

Two dependency-free Node processes:

- **`board.mjs`, the channel shim.** The session loads it as an MCP stdio server that declares `experimental["claude/channel"]`. Page events reach the session as `<channel source="board" id=… kind=…>` messages via `notifications/claude/channel`; the session answers with the `board_reply` and `board_seen` tools. The shim does nothing else: it forwards tool calls to the server and pushes from it.
- **`server.mjs`, everything else.** The page at `http://127.0.0.1:7880`, live updates over server-sent events (`/events`), presses, the gallery and notes. Cross-site posts are refused.

**Reloading.** The shim restarts `server.mjs` whenever the file changes (after `node --check`, so a half-saved file keeps the old server up) and tells the session the tool list changed. Open pages reload themselves when `board.html` changes, except while a note is being written. So only edits to `board.mjs` itself need `/mcp` → reconnect board; the channel instructions live there too.

The session starts the shim, so the board lives and dies with that session. Timings are appended to `~/.agentlink/board-log.jsonl`.

## Gallery (B1)

`node tools/board/render.mjs` runs every simulator script that has `shot` lines (about 45 s for 42 screens) and writes the PNGs to `build/board/<ui-sha>/`, which is git-ignored. `<ui-sha>` is the last commit that touched `firmware/` or `simulator/`, plus `-dirty` if those have uncommitted changes. When HEAD moves past the rendered build, the page marks the gallery stale and offers Re-render. Groups and labels come from `design/board/states.json`; a shot it doesn't list appears under "Other". 
## Notes (B2)

Open a screen in the Gallery and tap where something is wrong: a pin drops at that spot on the 466 px panel and you write the note. Notes collect as drafts in the tray at the bottom (on any number of screens) and **Send** pushes them to the session as one `kind="comments"` event listing each note's id, screen, build, pin position and PNG path. Each note then shows draft → sent → seen (the session called `board_seen`) → answered (`board_reply` with the note id), and the Notes tab lists them with their replies. Notes are stored in `design/board/comments.jsonl`, one per line, and committed with the branch.

## Checking the page

`drive.mjs` drives headless Chrome over the DevTools protocol (tap, run JS, screenshot); Chrome's own `--screenshot` fires before the page's data arrives. Run a second board on a spare port for tests: `sleep 600 | BOARD_PORT=7881 BOARD_TAILSCALE=0 node tools/board/board.mjs`.

## Run

From the worktree, in a terminal you keep open:

```sh
cd ~/projects/m5-agent-link-ui-v2
claude --mcp-config tools/board/mcp.json --dangerously-load-development-channels server:board
```

Accept the development-channels prompt, then open http://127.0.0.1:7880.

**Phone.** `mcp.json` sets `BOARD_TAILSCALE=1`, so the board also listens on this machine's Tailscale address: `http://<tailscale ip -4>:7880`, e.g. `http://100.74.203.10:7880`. Only devices on your tailnet can reach it, and it never binds 0.0.0.0. Anyone who can reach the page can send text into the session, so keep it tailnet-only.

**Relaunch this session with the board.** The development-channels flag takes several values, so put `--` before a prompt:

```sh
claude --resume <session-id> --mcp-config tools/board/mcp.json \
  --dangerously-load-development-channels server:board -- "<first prompt>"
```

## What the docs say (code.claude.com/docs/en/channels-reference)

- **Queued while busy.** Events that arrive while Claude is busy queue and are delivered together on the next turn.
- **Interactive sessions only.** The development flag is ignored with `-p` and in the Agent SDK.
- **Protocol version.** The v2 runtime can't deliver channel messages over MCP revision 2026-07-28, so the server never agrees to that version.
- **Prompts stay in the terminal.** Permission prompts still appear in the terminal. A `claude/channel/permission` relay exists, but it's not used here.
