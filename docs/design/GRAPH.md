# Agent graph (proto 0.22, issue e3d4)

A `git log --graph` for one agent session: the tool calls of the current turn as glyphs on lanes. Lane 0
is the session's own agent. Each sub-agent gets its own lane, which forks off lane 0 at the call that
started it and merges back when the sub-agent stops. This is the reference screen for the project tenet
**glyphs over words** (README Principles 4, UI-V3 §1.7): the graph has no text at all.

```
  ●          newest at the top
  │ ◉        a sub-agent still working (running node: hollow ring, pulses)
  │ ●
  ●─╯        merge: the sub-agent stopped after this call
  │ ●
  ●─╮        fork: the parent's agent call, then the sub-agent's first call
  ●
  ✕          a failed call (red)
```

## 1. What a node is

One node is one tool call: a `PreToolUse` / `preToolUse` creates it as running, and its `PostToolUse` /
`postToolUse` / `PostToolUseFailure` settles it as ok or failed. The two are paired by the call's id:
`toolCallId` for Copilot CLI, `tool_use_id` for Claude Code. Without an id, the result goes to the newest
running node with the same tool name in the same lane.

**Kind** (the glyph). The tool name decides it, compared without case:

| Kind | Code | Tools |
|---|---|---|
| other | 0 | anything not below |
| read | 1 | `view`, `Read`, `read_file`, `NotebookRead` |
| edit | 2 | `edit`, `create`, `Write`, `Edit`, `MultiEdit`, `NotebookEdit`, `apply_patch` |
| shell | 3 | `bash`, `powershell`, `write_bash`, `read_bash`, `stop_bash`, `BashOutput`, `KillShell` |
| search | 4 | `grep`, `glob`, `LS`, `lsp`, `*code_search` |
| web | 5 | `web_fetch`, `WebFetch`, `WebSearch`, `web_search` |
| agent | 6 | `task`, `Task`, `Agent`, `read_agent`, `write_agent`, `list_agents` |
| ask | 7 | `ask_user`, `AskUserQuestion` |
| plan | 8 | `TodoWrite`, `sql`, `update_todo`, `report_intent`, `ExitPlanMode`, `EnterPlanMode` |
| git | 9 | a shell call whose command starts with `git` or `gh` |
| test | 10 | a shell call the daemon recognises as a test run (DONE-SUMMARY.md) |
| mcp | 11 | `mcp__*` (Claude Code), or a name with a `-` server prefix (Copilot CLI) |

**State:** 0 running, 1 ok, 2 failed (a failure, a denial or a rejection). Some calls never get a
result, such as a Claude Code call denied at its permission prompt. When the parent's turn ends (`Stop`,
`agentStop`, `StopFailure`, `errorOccurred`), every call still running becomes failed.

## 2. Lanes

- **Which sub-agent.** Copilot CLI: a hook line whose `parentAgentId` differs from its `sessionId` comes
  from sub-agent `agentId` (type `agentType`). Claude Code: a tool hook with `agent_id` comes from that
  sub-agent (type `agent_type`).
- **Fork.** A sub-agent's first node takes the lowest free lane from 1 to 7 and carries the fork bit.
  When all seven are taken, further sub-agents share lane 7, which merges when the last of them stops.
- **Merge.** A sub-agent stops on Copilot's `agentStop` (from the sub-agent) or Claude Code's
  `SubagentStop`. Its last node gets the merge bit and its lane becomes free again. When the parent's own
  turn ends (`Stop`, `agentStop`), every lane still open merges.
- **Lane kind** (`la`, the glyph at the fork): 0 unknown, 1 explore, 2 task, 3 general-purpose,
  4 code-review, 5 research, 6 plan, 7 other. These come from `agentType` / `agent_type`, compared
  without case (`Explore` = explore, `Plan` = plan).
- **A new turn.** The parent's prompt (`UserPromptSubmit`, `userPromptSubmitted` without a `parentAgentId`)
  clears the graph and frees every lane. A sub-agent that is still working forks a new lane at its next call.

Claude Code needs the `SubagentStop` hook for merges. `agentlink hooks print` includes it; without it, a
sub-agent's lane merges when the turn ends.

## 3. Wire (PROTOCOL.md §6h)

- `cmd {op: graph (21), sid}` asks for a session's graph. The daemon answers with one `graph` message
  (`mt_graph` = 28, `re` = the cmd's id), or `result stale` for a session that is gone.
- `graph {0: sid, 1: nodes, ?2: la, ?3: drop, ?4: gen}`. `nodes` holds at most the newest 64 calls of the
  turn, oldest first. `drop` counts the older ones left out. `gen` changes whenever the graph changes.
- Each node is a single uint, with these bits from low to high: 0-3 kind, 4-5 state, 6-8 lane,
  9 fork, 10 merge.
- No agent content (no commands, paths or text), so the graph is sent under every privacy level and may
  be traced.
- The daemon serves `graph` to devices whose hello says proto ≥ 0.22, and the device sends `cmd graph`
  only to a daemon whose welcome says ≥ 0.22.

## 4. The screen (firmware)

- **Opening it.** A tap on the deck card's graph strip (§5), or "Graph" on the AGENT wheel (glyph:
  git-fork), opens it for an agent session, not a job, when the daemon speaks 0.22. B long, the A+B chord, swipe right and long-press go back, as on
  the other screens.
- **Fetching.** The device sends `cmd graph` when the screen opens, again when the session's `n` or `ok`
  changes (at most once a second), and every 3 s while the screen stays open.
- **Layout.** Newest at the top. Rows are spaced evenly; lanes sit in columns centred on the round
  display. A node is a filled disc in its lane's colour with the kind glyph inside. A running node is a
  ring that pulses, and a failed node is red. Lane lines are vertical. Fork and merge edges are short
  curves to lane 0. The fork node shows the lane kind's glyph next to it.
- **Moving.** The rim scrubs through the rows, one detent per row. A drag in the centre scrolls. Where
  `drop` > 0, the oldest row ends in a fading lane line.
- **Text.** None. The header shows the agent's state glyph, the same as on the AGENT screen.
- **Drawing.** One LVGL object with a draw callback paints the visible rows. There are no per-node
  objects and no canvas, which keeps DRAM small.

## 5. The deck strip (firmware)

The graph also lives on the session's deck card, so most of the time you never open the screen.

- **Where.** In the card's exception slot (300 × 56, centred on y 316), when nothing outranks it. The
  order is: offline chip, low-battery chip, the Answer / Review / Read button, **the strip**, the quiet
  glyph. It shows only for an agent session (not a job, not a merged Done group) with at least one call,
  on the full card, not the ambient face.
- **Layout.** The graph turned on its side: time runs left to right, with the newest call at the right
  edge. Lane 0's calls are discs (r 13) with their kind glyph. A sub-agent's calls are dots (r 5) on a
  row above: lane 1 on the first row, lanes 2–7 sharing the second. Forks and merges are short curves to
  lane 0. Running calls pulse and failed calls are red, as on the screen. Calls that don't fit, and
  `drop`, fade lane 0 out at the left edge.
- **Fetching.** The same `cmd graph`: when the card starts showing the session, when its `n` or `ok`
  changes (at most once a second), and otherwise every 10 s. Nothing is asked for on the ambient face,
  under a request, or while another screen is open.
- **Opening the screen.** A tap on the strip opens GRAPH (§4). A tap anywhere else on the card opens
  AGENT, as before.
