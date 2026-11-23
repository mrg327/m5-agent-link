# daemon/integrations/pi

The pi coding agent extension. pi has no shell hooks; it loads TypeScript extensions from
`~/.pi/agent/extensions/`. This one maps pi's events to Claude-shaped hook events and sends each to
`agentlink-hook pi <Event>`. Design: [PI.md](../../../docs/design/PI.md).

## Install

```sh
agentlink hooks pi > ~/.pi/agent/extensions/agentlink.ts
```

The CLI embeds `agentlink.ts` with `include_str!` (`daemon/crates/agentlink/src/hooks.rs`), so a
change here ships with the next `agentlink` build.

## Build and test

```sh
scripts/check daemon                     # includes: node --test daemon/integrations/pi/ (Node 24)
```

| File | Concern |
|---|---|
| `agentlink.ts` | The extension: event mapping, a bounded queue, the drain on shutdown |
| `agentlink.test.ts` | `node:test` tests of the mapping and the queue, with a fake spawn |

Environment: `AGENTLINK_HOOK` overrides the shim path, `AGENTLINK_PI=0` disables the extension.
Follow [js.md](../../../docs/conventions/js.md).
