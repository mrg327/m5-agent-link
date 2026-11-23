# agentlink-hook

The tiny shim that agent hooks call: `agentlink-hook <agent> <event>` with the hook JSON on stdin.
It forwards one line to the daemon socket and never holds up the agent. Also the shell-integration
call (`shell`, [JOBS.md](../../../docs/design/JOBS.md)) and Claude Code's status line (`statusline`,
[GAUGES.md](../../../docs/design/GAUGES.md)).

## Build and test

```sh
scripts/check daemon                     # fmt, clippy -D warnings, cargo test --workspace
echo '{"sessionId":"s1","toolName":"bash"}' | cargo run -p agentlink-hook -- copilot preToolUse
```

The e2e scripts in [`daemon/tools/`](../../tools/README.md) pipe real hook payloads through it.

| File | Concern |
|---|---|
| `src/main.rs` | The whole shim: read stdin, add agent and terminal env, send, print a decision if one comes back |

## Rules that bind it

- S-CRATE-03: depends on no workspace crate and speaks JSON, not the wire protocol.
- Fail-open: a hard deadline (1.5 s), every failure swallowed, exit code always 0, stdout empty unless
  the daemon returns a decision. Only Claude Code's `PermissionRequest` waits for the answer window
  ([ANSWER-WINDOW.md](../../../docs/design/ANSWER-WINDOW.md)).
