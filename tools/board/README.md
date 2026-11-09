# Board (proof of concept)

A local page that talks to a running Claude Code session with no claude.ai artifact in between.

`board.mjs` is one dependency-free Node process with two jobs:

- **Channel server.** It is an MCP stdio server that declares `experimental["claude/channel"]`. Each press on the page is sent into the session as a `<channel source="board" id=… kind=…>` message through `notifications/claude/channel`. The session answers with the `board_reply` tool.
- **Web server.** It serves the page at `http://127.0.0.1:7880`, with live updates over server-sent events (`/events`) and presses at `POST /press`. Cross-site posts are refused.

The session starts the server, so the board lives and dies with that session. Timings are appended to `~/.agentlink/board-log.jsonl`.

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
