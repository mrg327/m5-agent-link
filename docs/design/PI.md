# pi coding agent

Issue 20261116-f705. pi (`@earendil-works/pi-coding-agent`, checked with 0.85.1) has no shell hooks. Instead, it
loads TypeScript extensions from `~/.pi/agent/extensions/`. The watch reaches pi through one such extension.

## Install

```sh
agentlink hooks pi > ~/.pi/agent/extensions/agentlink.ts    # or test once: pi -e daemon/integrations/pi/agentlink.ts
```

The source is `daemon/integrations/pi/agentlink.ts`; the CLI embeds it. Its tests run with
`node --test daemon/integrations/pi/`.

## How it works

The extension turns pi's events into Claude-shaped hook lines and passes each one to
`agentlink-hook pi <Event>`, with the JSON on stdin. This means the daemon reuses its Claude Code handling.

| pi event | hook line | watch |
|---|---|---|
| `session_start` | `SessionStart` | a session appears |
| `before_agent_start` | `UserPromptSubmit` | work; the turn tally starts |
| `tool_execution_start` | `PreToolUse` (`ask_user_question`: a question) | the step / input |
| `tool_execution_end` | `PostToolUse` / `PostToolUseFailure` | the step result |
| `agent_end` | `Stop` with `last_assistant_message` | done, the summary, the last message |
| `session_shutdown` | `SessionEnd` | the session goes |

- **Ordering:** events are sent one at a time, in order, through a queue of at most 256.
- **Timeouts:** a hook that runs longer than 3 s is killed.
- **Missing hook binary:** if `agentlink-hook` is not installed, the extension stops sending.
- **Disabling:** set `AGENTLINK_PI=0` to turn the extension off. `AGENTLINK_HOOK` overrides the hook binary's path.

The daemon counts files and lines for the done summary from pi's own tool shapes (`turn.rs` `effect_of_pi`):

- `edit {path, edits:[{oldText,newText}]}`
- `write {path, content}`
- `bash {command}`: a failure ends with `Command exited with code N`.

## Protocol

pi is `agent_pi = 4` (proto 0.23). The daemon sends pi sessions to older watches as `other`, because their
decoders reject an unknown agent and would drop the whole frame.

## Later phases

These are not done yet:

- Replies and dictation from the watch, through `pi.sendUserMessage`.
- Answering questions from the watch.
- An opt-in approval gate on pi's blocking `tool_call` event.
- Remote install.

The watch shows pi with the same marks as the other agents. It needs its own glyph in the Sigil UI
(the `glyphs` branch).
