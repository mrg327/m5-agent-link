# Standing rules: "always allow this" from the watch

> Status: built on branch `next` (issue 20261112-7f85). Protocol 0.13. Run against the real hook and daemon
> with fake devices (`daemon/tools/standing_rules_e2e.py`) and in the simulator; not yet on the device or against
> a live Claude Code session.

The owner's ask: "Always allow `npm test` in this project" as a third option, so the same harmless request stops
coming back. The approval-safety findings behind the rules below: an "Accept all" whose scope was wider than the
user thought, and an "Allow for session" placed where a mis-tap hit it.

## 1. What the user gets

| Where | What |
|---|---|
| ASK, default view | Unchanged: `Open on PC` · **Approve** · `Deny`. |
| ASK, B twice (or a tap on the row) | **Always allow**, the third option, after Deny. While it is selected the request text makes way for the rule: the title says where it is saved (`Saved for this project` / `Until this session ends`), the platter shows the rule exactly as Claude Code will save it (`Bash(npm test:*)`), and a caption says how wide it is (`Wide: * matches anything` in orange, or `Exactly as written`). |
| The hold | 1.6 s, started on that row. A tap says `Hold to always allow`. |
| After | `Always allowed`, the rule's first line, and where it was saved, for 2.5 s; the audit log has a `standing_rule` line. |
| When the watch cannot show the rule whole | The row says `This rule is too long to show on the watch` · `Answer it on your computer`, and a hold says `Rule too long for the watch`. |
| When nothing safe is suggested | No row: Approve / Deny as before. |

## 2. What existed before (2026-11-12)

- **Claude Code → hook.** A `PermissionRequest` hook input carries `permission_suggestions` (below).
- **Daemon.** `broker::request_of` turned any non-empty `permission_suggestions` into three options, `Yes` /
  `Yes, don't ask again` / `No`, with `grant_mask` 0b011. The watch showed the label and nothing else: not the
  rule, not where it would be saved, not that a suggestion might be a mode change.
- **Hook → Claude Code.** On that option the hook printed `updatedPermissions` = **the whole
  `permission_suggestions` array**, verbatim. Whatever Claude Code had suggested was applied unseen: every rule in
  it, at whatever destination it named, and any `setMode` entry (an `Edit` request's suggestions can carry
  `setMode` `acceptEdits`, which stops every edit prompt for the session).
- **Copilot CLI** (`prompt.rs`): its prompt is read off the screen. `2. Yes, and don't ask again for `touch` in
  this repo` became a granting option, labelled `Yes, don't ask again` when the words matched, else cut to 24
  characters (`Yes, and don't ask a…`), and answered by typing its digit. The watch could not know what it saves.

That is the gap: a one-hold grant whose content and scope the user never saw.

## 3. What Claude Code does (checked 2026-11-12)

From the [hooks reference](https://code.claude.com/docs/en/hooks) (PermissionRequest input, decision control,
"Permission update entries") and [permissions](https://code.claude.com/docs/en/permissions):

- Input: "An optional `permission_suggestions` array contains the permission updates Claude Code suggests for this
  request, such as adding an allow rule or changing the permission mode." The documented example:
  `{"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "rm -rf node_modules"}], "behavior": "allow",
  "destination": "localSettings"}`.
- "The `permission_suggestions` array isn't an exact list of the options you see": some dialogs do not read it, and
  a dialog can withhold an option whose suggestion stays in the array (for example under
  `allowManagedPermissionRulesOnly`).
- Output: `hookSpecificOutput.decision` with `behavior` `allow`, and for `allow` only, `updatedPermissions`: "array
  of permission update entries to apply". "A hook can echo one of the `permission_suggestions` it received as its
  own `updatedPermissions` output."
- Entries: `addRules` / `replaceRules` / `removeRules` (`rules` of `{toolName, ruleContent?}`, `behavior`
  `allow`/`deny`/`ask`), `setMode` (`mode`), `addDirectories` / `removeDirectories`. "Omit `ruleContent` to match
  the whole tool." `Bash` and `Bash(*)` are both `{ "toolName": "Bash" }`.
- `destination`: `session` (in memory, until the session ends), `localSettings` (`.claude/settings.local.json`),
  `projectSettings` (`.claude/settings.json`), `userSettings` (`~/.claude/settings.json`).
- Rules: `Bash(npm test:*)` and `Bash(npm test *)` match the same commands, `npm test` included; "The permission
  dialog writes the space-separated form". A `*` elsewhere stands for anything in its place (`Bash(git * main)`
  also matches `git -c core.fsmonitor=<script> diff main`). A compound command saves "a separate rule for each
  subcommand", "up to 5 rules". The dialog's own "don't ask again" for Bash saves to `.claude/settings.local.json`
  at the repository root, for every session in that repository and its worktrees; and Claude Code "offers those
  options only when the prompt can show you everything they would allow".
- Removing: `/permissions` "lists all permission rules and the `settings.json` file each rule comes from", and a
  change there applies from Claude's next tool call.

## 4. What the watch may grant (`daemon/crates/agentlinkd/src/standing.rs`)

**One of Claude Code's own suggestions, verbatim, and only one the watch can show in full.** An entry is offered
only when all of this holds; otherwise it is ignored:

- `type` `addRules`, `behavior` `allow`, and no key besides `type`, `rules`, `behavior`, `destination` (an entry
  this build does not fully understand is not passed back half understood).
- `destination` `session` or `localSettings`.
- One to five rules, each exactly `{toolName, ruleContent}`; the tool name letters, digits, `_` and `-`.
- Every `ruleContent` printable ASCII, no space at either end, and no `*` except one trailing wildcard (` *`, `:*`
  or `/**`) after a non-empty literal.
- All the rules, `Tool(content)` one per line, within 160 bytes.

Of the entries that pass, the narrowest destination wins (session before project), the first among equals. The
daemon never writes a rule of its own; it hands back the entry Claude Code sent, and the hook checks again that
every entry it is asked to apply is one it received (`agentlink-hook` `hook_output`): anything else is no decision
at all, and the terminal asks.

**Scope.** `session` reads `Until this session ends`; `localSettings` reads `Saved for this project` (the owner's
"in this project": the user's own file, every session in that repository). Two are never offered:

- `userSettings`, all projects: **no.** One hold on a wrist would then change what every agent may do in every
  repository, in a file the user rarely looks at, from a screen that cannot show which other projects it reaches.
  That is the "Accept all" failure. It stays one `/permissions` away at the keyboard.
- `projectSettings`, `.claude/settings.json`: **no.** It is usually checked in, so the grant would reach
  teammates through a commit; "this project" on a watch would hide that.

When Claude Code suggests both a session and a project entry, the watch offers the session one; a wider choice on
the watch would need a scope picker, a second row, and the user to read which is which, and the default view stays
calm instead. For Bash Claude Code suggests `localSettings`, so "in this project" is what the owner gets.

**Breadth.** A rule that names the whole tool (no `ruleContent`, `*`, `:*`) is **never offered**: one hold should
not hand over every shell command. A wildcard inside a rule (`git * main`, `* --version`, `npm*`, a `*.` domain) is
**not offered** either: it can match far from what the user read (the docs' own `-c core.fsmonitor` example). A
single trailing wildcard (`npm test:*`, `Read(//srv/data/**)`) is offered and **marked**: `wide` on the wire and
`Wide: * matches anything` in orange on the watch. A rule without a wildcard reads `Exactly as written`.

## 5. On the watch (`ui_ask.c`, `nav.c`, UI-V3 §4.3c)

- **The row.** `Always allow`, orange, a hold item (NAV_GRANT_HOLD_MS 1600 ms, longer than Approve's 1200 ms; the
  daemon refuses less than 1500 ms). It comes after Deny, two steps from the default, so the plain Approve stays
  the one under the finger and a mis-tap cannot land on it from the default view (a tap never grants anyway).
- **The rule panel.** In place of the request platter while the row is selected: the rule in `FONT_BODY` white
  (up to two lines, 292 px), the breadth caption in `FONT_CAPTION`, and the scope as the title (`FONT_HEAD`). No
  ellipsis anywhere: the rule label clips rather than dots, and the watch measures first. A rule over two lines
  or a scope title wider than the title row locks the row (`This rule is too long to show on the watch` ·
  `Answer it on your computer`; a hold says `Rule too long for the watch`). The daemon's 160-byte cap keeps
  most rules inside two lines.
- **What the watch accepts.** `rule`, `scope` and `wide` only from a daemon whose welcome says 0.13, only on an
  approval with Approve in `acts`, the rule whole (≤ 160 bytes, not cut by the model's buffer), every byte
  printable ASCII or a line break (no NUL, no control, no UTF-8 that could draw as something else), no line
  break at either end, a known scope. Anything else: no row.
- **Read first.** The row grants, so the request text lock of UI-V3 §4.3a applies to it as to Approve.
- **Bound to what is on screen.** The watch keeps its own copy of the rule it shows and the FNV-1a hash of that
  copy, its scope and its breadth; the hold records that hash when it starts and is cancelled when the request's
  rule, scope or breadth changes, when the request is answered, replaced, cleared or handed back, and when its
  answer window runs out. The commit sends `approve {rid, hold_ms, rh}` with that hash; the daemon saves the rule
  only when it is the hash of the rule it offered for that rid. A request the daemon re-sends opens again at
  Approve, so a changed rule has to be selected, and so shown, again.
- **After.** `Always allowed` (`FONT_TITLE`), then the rule's first line (` +` when there are more) and the scope
  in the caption, for at least 2.5 s.

## 6. Audit, and taking a rule back

- **Audit.** Each grant appends to `~/.agentlink/audit.jsonl`:
  `{"ts", "kind": "standing_rule", "sid", "rid", "msg": {"rule", "scope", "destination", "wide", "session"}}`
  (session = the source key, `claude:<session_id>`). The tool input and its output are not in it. The usual
  `approval` line with the command follows as for any approve.
- **Undo: not feasible.** Claude Code applies `updatedPermissions` when the hook returns, and nothing outside
  Claude Code can take an entry back: a `removeRules` entry is only accepted in a later `PermissionRequest`
  decision, and with the rule in place Claude Code will not ask for that command again. A `session` rule lives in
  Claude Code's memory, out of reach; editing `.claude/settings.local.json` from the daemon would race Claude
  Code's own writes and is not a supported path, so the daemon writes no settings file. The watch therefore
  shows exactly what will be saved, before the hold, and makes that hold longer.
- **How to remove a rule.** In Claude Code, `/permissions` lists every rule with the file it comes from; remove it
  there (it applies from the next tool call). Or delete the line from `permissions.allow` in
  `.claude/settings.local.json` at the repository root. A session rule ends with the session.

## 7. Other agents

Copilot CLI has no hook that takes a decision; its prompt is read off the screen and answered by typing. Its
`Yes, and don't ask again for … in this repo` names the rule in Copilot's own words, which the daemon cannot
verify or show as a rule, so that option is **not offered on the watch** any more (`prompt.rs`): the card offers
`Yes` and `No`, and the terminal keeps the rest. Any agent without suggestions: no row.

## 8. Protocol 0.13

```cddl
attention.13 rule:  tstr   ; the standing rule, as Claude Code saves it (≤ 160 B)  ┐ always together, only to
attention.14 scope: uint   ; 0 session, 1 project (rule_scope)                     │ devices whose hello says
attention.15 wide:  bool   ; it ends in a wildcard                                 ┘ minor >= 13
cmd.5        rh:    uint   ; with approve: save the rule; FNV-1a 32 of rule, 0x00, scope, wide
```

| watch \ daemon | 0.12 or older | 0.13 |
|---|---|---|
| 0.12 or older | as before (`Yes, don't ask again` echoes every suggestion) | Approve / Deny; no rule keys, no options; nothing is saved |
| 0.13 | as before (the old daemon's option is shown and answered as before) | Always allow, as above |

A 0.13 daemon never sends the old `Yes, don't ask again` option to any watch, and a 0.13 hook no longer echoes the
suggestions on the older daemon's `"grant": true` (it allows once). An older daemon ignores `rh` and approves once.
Vectors: `attention_standing_rule`, `cmd_approve_rule` (rh `0x516a52ed`). `scope` is a uint on the wire so a scope a
device does not know drops the rule, not the request.

## 9. Safety, and the tests that try to break it

The invariants of UI-V2 and ANSWER-WINDOW.md stay: a tap never approves, denies or grants; a granting row needs a
hold that starts on that row, bound to the request id and armed 300 ms after the card appears; an expired, replaced
or handed-back request can never be answered. New: a grant names the exact rule shown; the daemon hands back only
an entry Claude Code sent, the hook only one it received; nothing on a granting row is cut.

- daemon: `standing.rs` (the docs' example verbatim; whole-tool, `*`, inner wildcards, control, non-ASCII, padded,
  over-long, extra keys, wrong behaviour or destination, `setMode` / `addDirectories` / `replaceRules` /
  `removeRules` refused; narrowest wins; the hash binds text, scope, breadth), `broker.rs` (granted verbatim; a
  plain approve saves nothing; another request's rule, another scope, no rule: denied and still open; after
  expiry, hand-back or a terminal answer: stale), `device.rs` (0.12 gets no keys; the approve hold, a wrong hash, a
  hash past u32, a wrong rid all refused; the audit line), the hook (only a received suggestion, else no decision;
  the legacy grant allows once).
- simulator: `sim_rule_grant_hold`, `sim_rule_session_exact`, `sim_rule_swapped` (the rule re-sent mid-hold: no
  grant of the old rule; the new one after it is shown), `sim_rule_bound` (the request answered on the PC and
  replaced mid-hold; the window running out mid-hold and after), `sim_rule_too_long` (three lines, 165 bytes, a
  tab, an unknown scope), `sim_rule_old_daemon`, `sim_rule_read_first`.
- end to end: `daemon/tools/standing_rules_e2e.py` (the real hook and daemon, fake 0.13 and 0.12 devices).

## 10. Not in this version

- The device itself, and a live Claude Code session: the shapes are the documented ones, the hook's output has not
  been fed to a real Claude Code.
- A scope picker on the watch (a project rule when Claude Code suggests both), and showing a long rule in the
  reader instead of locking it.
- Copilot's own "don't ask again", shown in its own words through the reader.
- Whether a `localSettings` rule saved under `allowManagedPermissionRulesOnly` has any effect (the docs say the
  dialog hides the option; the watch would still offer the suggestion).
