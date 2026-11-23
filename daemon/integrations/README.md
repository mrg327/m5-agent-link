# daemon/integrations

Extensions that let an agent without shell hooks reach the daemon. Each one forwards the agent's
events to `agentlink-hook`, the same way Claude Code and Copilot command hooks do.

| Path | Agent | Design |
|---|---|---|
| [`pi/`](pi/README.md) | pi coding agent (TypeScript extension) | [PI.md](../../docs/design/PI.md) |

Run their tests with `scripts/check daemon` (Node 24). A new integration gets a directory here with
its own README and tests, and a `hooks` subcommand in the `agentlink` CLI that prints or installs it.
