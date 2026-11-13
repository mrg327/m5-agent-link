# Last message and questions: read what the agent said, answer what it asks

> Status: built on branch `response` (issues 20261113-da11 and 20261113-bcd9). Protocol 0.18. Run against the
> real hook and daemon with fake devices (`daemon/tools/last_message_e2e.py`) and in the simulator; not yet on
> the device or against a live Claude Code session.

The owner's ask: "when I step away from my pc with the watch, I need to be able to see what the agent said in
order to be able to do something like dictate from across the room." And: when Claude Code asked a question
with `AskUserQuestion`, the watch "was only able to 'open on PC', not able to respond on it".

## 1. What the user gets

| Where | What |
|---|---|
| The finished card (HOME, a Claude session that is done) | The first lines of the agent's final message in place of the step text, and a `Read` button. No pop-up, no extra buzz. |
| `Read` (the button, or `Read` in the agent menu) | The message in the paged reader (seven lines a page, `2 of 6`), fetched from the daemon as it is read. Markdown is cleaned to plain text. |
| The end of the message | `Reply` and `Dictate` under the last page: the Reply list (the closing question's answers first, then the user's quick replies) or the voice flow, which types into the session (or queues it if the agent is working again, proto 0.17). |
| A question from the agent (`AskUserQuestion`) | One question at a time: its header and text, its options as rows. Single choice: hold the row you want. Several choices: tap rows to tick them, then hold `Send`. `Other` dictates a free answer. With several questions, each answer moves to the next one and the last sends them all together. The terminal's own question appears if the watch does not answer within the answer window. |

Copilot CLI's stop hook carries no final message, so nothing new appears for it. Under privacy `meta` the card
shows no text and the reader is not offered (the daemon says only that a message exists).

## 2. The last message (issue da11)

### 2.1 Source and lifetime

- Claude Code's `Stop` hook input has `last_assistant_message`: "the text content of Claude's final response"
  ([hooks](https://code.claude.com/docs/en/hooks), checked 2026-11-12). Only `Stop` is used: `StopFailure`
  carries the API error string in that field, and a sub-agent's stop is not its parent's.
- The daemon keeps **one message per session**, the last one, in memory only (lost on a daemon restart, dropped
  with the session). A new `Stop` replaces it. A new prompt (`UserPromptSubmit`) **clears** it: from then on the
  message describes a turn that is over, and the card shows the agent working instead. (Decided 2026-11-12. The
  alternative, keeping it until the next stop, would let a stale answer sit next to a running turn.)
- Only the final message, never the text between tool calls (the owner's choice).

### 2.2 Cleaning (`agentlinkd/src/lastmsg.rs`)

The watch has no markdown renderer, so the daemon turns the message into readable plain text:

| Markdown | On the watch |
|---|---|
| `# Heading`, `**bold**`, `*em*`, `_em_`, `` `code` `` | the words alone (`__name__` around one identifier is kept: it is a name) |
| `- item`, `* item`, `+ item` | `• item` (nested: two spaces per level) |
| `1. item` | unchanged |
| `> quote` | the text |
| fenced code block | its lines, unchanged, at most 6 of them, then `  … N more lines` |
| table | one line per row, cells joined by ` · `; the separator row dropped |
| `[text](url)`, `![alt](url)` | `text`, `alt` |
| `---` | a blank line |
| two or more blank lines | one |

Then the text is made drawable like the request reader's (`broker::detail_text`), except that a few common
symbols get a drawable stand-in (`→` `->`, `⇒` `=>`, `≥` `>=`, `≠` `!=`, `✓` `✔` `✅` `[x]`, `❌` `✗` `[ ]`, `▸` `•`) and emoji and other characters outside
the watch's glyph set are dropped instead of spelled out: this is something to read, not a command to approve.
At most **4096 bytes** (the reader's buffer, `DETAIL_CAP`), cut on a line or character boundary with `…`.

### 2.3 Protocol (0.18)

```cddl
session.36 lm: uint    ; the last message's id, new for each message; 0 = none        ┐ only to devices
session.37 ll: uint    ; its length in bytes as cleaned (<= 4096)                      │ with proto
session.38 lp: tstr    ; its start, one paragraph, <= 160 bytes; "" under privacy meta ┘ >= 0.18
detail.5   lm: uint    ; on chunks of a last message (no rid)
cmd_op_detail {sid, arg: off} with no rid  ; the session's last message from byte off
```

- The preview is the first paragraph (and the next one when the first is a heading-like line), whitespace
  collapsed, cut between words with `…`. A stop whose message cleans to nothing clears the old one.
- Keys 36-38 come with the delta that enters done (and in a snapshot) and are cleared with `lm: 0, ll: 0,
  lp: ""` (a delta cannot remove a key) when a prompt starts.
- `cmd detail` without a `rid` from a device with proto >= 0.18 asks for the session's last message; the
  daemon answers with `detail {sid, text, off, total, lm}` chunks of at most 1024 bytes, cut on UTF-8
  boundaries, then `result ok`. `stale` when the session has no message, `denied` under privacy `meta` or for an
  offset inside a character. A device below 0.18 never asks: there it is `denied` (`rid required`) from 0.11 and
  `unsupported` below, as before. With a `rid` it is the request text, as before.
- **Bound to the message.** The device keeps a chunk only if it names the session it asked for, the message id
  it showed (`lm`), the offset it has reached and the length the session announced (`ll`). A new message (new
  `lm`) starts over; a chunk of another one is dropped.
- **Privacy.** Agent content: `lp` and the chunks' `text` never reach the audit log or the trace (`<private>`,
  as for `q`, `rp` and `qd`), and the Stop hook line's `last_assistant_message` is redacted before it is traced.
  Under privacy `meta`, `lm` and `ll` are still sent (a message exists), `lp` is `""` and `detail` is refused.
  The `decision` audit line says `<private>` for a question's answers. A question's own text and options (`qs`)
  are traced like any request's `title` and `text`.
- Older devices get none of it. A 0.18 watch on an older daemon never sees `lm`, so it shows the card as before.

## 3. Questions (issue bcd9)

### 3.1 What Claude Code sends and expects (checked 2026-11-12)

From the [hooks reference](https://code.claude.com/docs/en/hooks):

- `AskUserQuestion`'s input (the daemon accepts 1-4 options per question, as the CDDL does): `questions`, "each with a `question` string, short `header`, `options` array, and
  optional `multiSelect` flag" (options are `{label, description}`; 1-4 questions, 2-4 options each).
- `answers`: "Maps question text to the selected option label. **Multi-select answers join labels with
  commas.** Claude doesn't set this field; supply it via `updatedInput` to answer programmatically."
- `PermissionRequest` decision: `updatedInput` "For `"allow"` only: modifies the tool's input parameters before
  execution. Replaces the entire input object … `AskUserQuestion` and `ExitPlanMode` need this field."
- "Returning `"allow"` alone is not sufficient for these tools."

From the owner's live trace (2026-11-12): Claude Code sends `PermissionRequest` with `tool_name`
`AskUserQuestion`, and after an answer in the terminal the tool's input was `{questions, answers, annotations}`
with `answers` = `{"<question text>": "<chosen label>"}`, one entry per question.

**Verified** (docs + trace): the hook event, the input shape, `answers` keyed by the question text with the
chosen label, `updatedInput` on a `PermissionRequest` allow. **Assumed**, not yet seen live: the separator for
several labels (the docs say "commas"; the daemon joins with `", "`),
that a free "Other" answer is the typed text itself, and that `annotations` may be left out (it is optional and
the daemon does not send it). The e2e test checks the hook output against this shape; the first live run must
confirm it.

### 3.2 The daemon

- **Held where approvals are held.** The `PermissionRequest` for `AskUserQuestion` is a held request with the
  answer window (ANSWER-WINDOW.md), like an approval: the terminal shows its own question when the window ends,
  on Open on PC, or when the hook goes away; first answer wins, and silence never answers. `PreToolUse` for
  `AskUserQuestion` no longer holds anything (the hook does not block on it; an older hook that still waits is
  answered "no decision" at once), so a question is never held twice.
- **The request.** `kind` question; `title` the header (or `2 questions`), `text` the first question, and the new
  `qs` (attention key 16) with every question: header (<= 16 bytes), question (<= 160 bytes), options (label
  <= 48 bytes, description <= 120 bytes) and `m` for multi-select. Descriptions are shortened, then dropped, if
  the message would pass 3 KB. A malformed question (no options, more than four of anything) is a notice: the
  card shows it with Open on PC only.
- **Answering: `cmd answer` (op 19)** `{sid, rid, picks, aids, hold_ms}`, one entry per question: `picks` a bit
  mask of the options picked, `aids` the recording whose transcript is the free answer (0 = none). Refused
  (`denied`) without a hold of 1000 ms, with the wrong number of entries, with no pick or more than one on a
  single-choice question, with a bit past the options, or with both a pick and a recording; `stale` for a
  request that is not open or a recording with no transcript. Nothing is used up by a refused answer.
- **What the agent gets.** `allow` with `updatedInput` = the original input plus `answers`: `{question: label}`,
  labels in option order joined with `", "` for several, the transcript for a dictated one. The hook passes it on
  only when every key is one of the request's own questions and every question has an answer; otherwise it
  prints nothing and the terminal asks. **Upgrade the hook and the daemon together**: an older `agentlink-hook`
  would pass a question's answer on as a bare `allow`, which Claude Code documents as not enough for this tool.
- **Malformed** (no options, more than four options or questions, a missing label, an empty or repeated question
  text): a notice with Open on PC only.
- **Older devices** (below 0.18) get no `qs`. One single-choice question keeps its options (`opts`, answered by
  `reply` with an index, or by `dictate` with the `rid`), as the old `PreToolUse` path offered; anything else is
  Open on PC only.
- **Audit.** The `approval` audit line has the command (indices and recording ids), never the labels or the
  transcript.

### 3.3 The watch

- ASK shows one question at a time: caption (`Claude Code · webapp`, `1 of 2` with several questions), the header
  as the title, the question in the platter (`Read all` and the reader when it is long), then the options as
  picker rows, then `Other` (a dictated answer, when the daemon transcribes) and `Open on PC`.
- **Single choice:** a hold on the option's own row (1.2 s, the approve hold, bound to the request and to the
  row under the finger) picks it. A tap only selects it and says `Hold to choose`.
- **Several choices:** a tap on a row ticks or unticks it (the label gets a check mark); `Send` (`Next` before
  the last question) is always in the list, so it never jumps, and is held to commit (`Tick at least one` until
  something is ticked).
- **Other:** opens VOICE for this question; the transcript's `Send` (`Use` before the last question) is held as
  usual, its pages read first (issue c6cf); it becomes that question's answer.
- **Several questions:** each answer moves to the next question; swipe right goes back to the previous one (its
  answer kept) and, on the first, leaves ASK. The last answer sends `cmd answer` with all of them. Nothing goes
  to the daemon before then.
- Options' descriptions: `Read all` under the question opens the reader with the question and every option's
  description (`React: Component library`).
- Safety invariants as UI-V3 §1: a tap never sends; holds are bound to the request on screen and to the row
  under the finger; taps are ignored for 300 ms after the screen opens; an expired or replaced request clears
  the answers in progress.

## 4. Tests

- daemon: `lastmsg.rs` (cleaning, caps, preview), `hook_source.rs` (Stop sets, prompt clears, StopFailure and
  Copilot do not), `device.rs` (gating by minor and privacy, detail of a last message, stale / denied /
  unsupported, chunks bound to `lm`, the trace never holds the text), `broker.rs` (the question request, the
  answer's validation and its `answers`, old-device fallbacks), the hook (`updatedInput` only for the request's own
  questions).
- simulator: `sim_last_message_card`, `sim_last_message_read`, `sim_last_message_reply`, `sim_last_message_meta`,
  `sim_question_single`, `sim_question_multi`, `sim_question_two`, `sim_question_other`, `sim_question_tap_never_sends`.
- end to end: `daemon/tools/last_message_e2e.py` (the real hook and daemon, a fake 0.18 device and a 0.17 one).
