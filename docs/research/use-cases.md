# Use cases and pain points for a watch/desk agent-control device

Research date: 2026-11-06. Method: web search snippets only (pages were not fetched in full), so
quotes are from search excerpts. Items marked **[inferred]** are my synthesis, not sourced.

## 1. Multi-agent workflows and what hurts

### How people run agents in parallel
- **Git worktrees + tmux panes/tabs** are the dominant pattern. Claude Code shipped native worktree
  support on 2026-02-21; people run 3-5 agents on one repo, one worktree each.
  - https://docs.bswen.com/blog/2026-03-21-git-worktrees-parallel-claude-code/
  - https://www.verdent.ai/guides/how-to-run-parallel-claude-code-agents
- **Remote/dev box + tmux** so work survives closing the laptop or losing Wi-Fi.
  - Show HN tmux-agent-switcher: https://news.ycombinator.com/item?id=49278395
  - It infers agent state by scraping tmux metadata and screen text. The author admits UI changes
    break this.
- **Cloud/background agents** (Copilot cloud agent, Codex cloud, Jules, Devin, Claude cloud sessions)
  run unattended. They still need a human for review, plan approval and clarifying questions.
  I did not research these individually, so this is thin evidence.
- **Hook systems** exist for both Copilot CLI and Claude Code.
  - Copilot hooks: https://docs.github.com/en/copilot/concepts/customizations/hooks
    - Hook events: `sessionStart`, `sessionEnd`, `userPromptSubmitted`, `preToolUse`,
      `postToolUse`, `agentStop`, `subagentStop`.
    - Config lives in `.github/hooks/*.json` or `~/.copilot/hooks/*.json`.
    - `preToolUse` can approve or deny tool calls.
  - Claude hooks: https://code.claude.com/docs/en/hooks-guide
    - The `Notification` event has a `notification_type` of `permission_prompt` and others.
    - The payload includes `message`.

### Pain points (with evidence)
| Pain | Evidence |
|---|---|
| Agent silently blocked on a prompt | "three of them are sitting on permission prompts you never saw, and the one that finished five minutes ago is waiting for you to notice" (https://www.developersdigest.tech/blog/agent-manager-tmux-tui-claude-code-codex-opencode) |
| No cross-agent view, so you cycle panes | Same HN thread: with plain tmux, finding which of five agents is blocked means cycling through every pane. A "priority queue of agents requiring intervention" was called the main value. |
| Copilot CLI looks busy while it is blocked | github/copilot-cli#2650: users switch to the editor or browser, and a tmux pane gives "no visible signal unless actively watching". https://github.com/github/copilot-cli/issues/2650 |
| Hook coverage gaps | github/copilot-cli#1651: "Plan Ready for Review" and permission prompts fired no hook; a ~3 min gap appeared between `postToolUse` and `exit_plan_mode`. Maintainers said v1.0.18 added `permission_prompt` and `elicitation_dialog` notification hooks. The CLI also emits a terminal bell (`beep` setting, default on). https://github.com/github/copilot-cli/issues/1651 |
| Notification spam | A commenter on #2650 complained that Copilot CLI "spams extremely many" notifications. The excerpt was cut off, so the exact cause is unverified. Signal-to-noise matters. |
| Fragile screen-scraping | tmux-agent-switcher author: "UI changes or process name changes can break it". Prefer structured hooks over scraping. |
| Tool sprawl | HN thread: authors of Herdr, agent-deck, tmux-agent-switcher and others, "we're all working on solving this problem". The space has no winner. |
| Approval fatigue | Anthropic: users accept 93% of permission prompts, so they stop reading them. The workarounds are `--dangerously-skip-permissions`, sandboxing, or auto mode. https://www.anthropic.com/research/claude-code-auto-mode |
| Context switching and merge conflicts | Verdent and BSWEN guides: same-directory agents clobber each other; each worktree needs its own setup; naming worktrees by task helps you tell them apart. |
| Cost visibility | Verdent guide: "three parallel agents burn roughly 3x credits". Users watch dashboards manually. |
| Long-running tasks | Remote-box users want to leave and be pinged. Evidence is the tmux-agent-switcher motivation (flights, closed laptop). |

## 2. Existing notification and remote-control solutions

| Solution | What it does | Shortcomings for our use |
|---|---|---|
| Terminal bell (`\a`, Copilot `beep`) | Zero-config audible or visual ping | Only works if the terminal is in range and not muted. No identity (which pane or machine). Needs a visible terminal. |
| OS notifications via hooks (osascript, notify-send) | Claude docs show a `Notification` hook running `osascript display notification "Claude Code needs your attention"`. Copilot has equivalent hooks. | Generic text. Per-machine only. Disappears after a few seconds. No action. Hook coverage varies by CLI and version. |
| ntfy / Pushover / Slack bots via hooks | Hook calls curl to a push service. **[inferred]** from the hook design; I did not fetch a specific ntfy source. | Read-only. Depends on a cloud relay (or self-hosted ntfy). No approve action. No grouping across agents. |
| Claude Code mobile / Remote Control | Claude app for iOS and Android. Cloud sessions, plus Remote Control of a local session, plus push notifications. Permission modes Manual, Accept edits, Plan and Auto from the phone; Bypass is **not** selectable. https://code.claude.com/docs/en/mobile | Claude-only. Cloud-account dependent. Needs the phone out of the pocket. |
| Happy (Coder) | Open-source. QR pairing with the phone. Send messages with images, answer permission requests, stop a turn, switch models, start sessions, view files and git changes. Voice coding and push notifications. E2E-encrypted sync through a relay. Supports Claude, Codex, Grok, Gemini (beta). https://happy.engineering/mobile-app/ and https://happy.engineering/docs/features/ | Uses a cloud relay (encrypted). Phone-sized UI, so reading diffs is still slow. Has no watch-level glance view in what I saw. |
| Omnara | Command center. Mobile and web monitoring, smart notifications when the AI needs input, remote launch, `omnara serve`. `--no-tunnel` gives a local-network mode. Free tier plus Pro $9/mo, per a third-party guide. https://github.com/CryptoJym/omnara-docs/blob/main/omnara-complete-guide.md | Third-party doc, not official, so treat as unverified. Cloud by default. |
| VibeTunnel | Browser-based terminal access. I did **not** research it, so no detail. | n/a |
| tmux TUIs (Agent-Manager, tmux-agent-switcher, cmux, agent-deck, Herdr) | Per-agent status icons, attention queue, answer blocked agents without attaching | Only visible if you are looking at that screen or machine. Often scrape output. Single host. |

**Feature matrix (what exists)** (✓ = reported; ? = not verified):
- Approve/deny from phone: Happy ✓, Claude mobile ✓, Omnara ✓.
- Quick/canned replies: not documented in my sources (?).
- Voice input: Happy ✓. Others ?.
- Session list: Happy ✓, Claude ✓, Omnara ✓.
- Token/cost display: not found in any source (?). This is a gap we could fill.
- Cross-machine, multi-CLI (Copilot + Claude + Codex) glanceable view that also raises the right window: **not found**. This is our niche.

## 3. Information value at a glance (466x466 round display)
Ranking is my judgment **[inferred]**, informed by the pain points above.

1. **State**: running / needs input / done / error. This is the primary signal and needs color, haptic and a distinct icon.
2. **Which agent**: short label made of repo + branch (worktree name) + machine name. Without this, an alert is not actionable.
3. **Pending request text** (for needs-input): the tool and a short command or path, truncated. The Anthropic data says users rubber-stamp, so showing the actual command matters.
4. **Elapsed time in state** ("waiting 4m"): drives prioritization.
5. **Current tool/step** while running: confirms it is progressing, not hung.
6. **Last message summary**: one line, optional, privacy-gated (section 5).
7. **Repo/branch** and **machine**: secondary detail on tap.
8. **Cost / context % / tokens**: low-frequency. Show as a ring or badge, with a threshold alert when context is near full or spend is high. Cost visibility is a stated worry (Verdent), but no hook payload guarantee exists, so it is best-effort.

Display design notes **[inferred]**: ring-of-dots overview (one dot per agent, colored by state, needs-input pulsing); a sorted attention queue (oldest-waiting first) as the default home; swipe between agents; one hardware button = "next needing attention".

## 4. Interaction primitives and risk

| Primitive | Value | Risk | Verdict |
|---|---|---|---|
| Acknowledge / dismiss alert | High | None | v1 |
| Focus window on the right PC | High: solves "which terminal" | Low (focus only, no input sent). Risk: focusing the wrong window if matching is flaky. | v1 |
| Notify me when done (one-shot subscribe) | High for long tasks | None | v1 |
| Snooze (per agent or global) | Medium; limits spam | Low. Danger is forgetting a blocked agent, so snooze should auto-expire and escalate. | v1/v2 |
| Approve / deny a permission | Highest value, but also highest risk | Medium-high. Users accept 93% of prompts, so a one-tap approve on a tiny screen worsens rubber-stamping. A prompt-injected command could be approved without reading it. | v2, with guardrails below |
| Canned replies ("continue", "yes", "stop and summarize") | Medium | Low-medium; text is fixed and pre-reviewed by the user | v2 |
| Pause / cancel (interrupt turn) | Medium | Low (conservative action), but may lose work mid-edit | v2 |
| Voice dictation to agent | Medium: nice for follow-ups | Medium: misrecognition, ambient capture, text reaches a tool-capable agent. Show the transcript and require confirmation. | v3 |
| Approve "always allow" / change permission mode | Low | High. Anthropic notes Bypass is deliberately not selectable from the phone app. | **Never** on watch |
| Free-form command execution | Low | Very high | Never |

**Approval guardrails** (proposal):
- Show the full tool name and the first ~80 characters of the command; require a long-press or a two-button press to approve.
- Deny is a single tap.
- Allow-list by risk tier: file reads and edits inside the worktree are approvable on the watch. Shell commands with network, `rm`, `git push`, or anything outside the repo get "Open on PC" only.
- Bind each approval to a request ID and nonce so a stale approve cannot apply to a newer prompt.
- Timeout means deny or no-op, never approve.
- The mechanism depends on the CLI. Copilot `preToolUse` hooks can approve or deny, so hook-based gating is feasible. For prompts rendered in the TUI, only keystroke injection is possible, which is fragile; verify before promising it.

## 5. Team and enterprise considerations
- **Local-only first.** Many corporate networks block third-party relays (ntfy.sh, Pushover, vendor tunnels). Design: host daemon on the LAN talks directly to the watch over Wi-Fi, with mDNS or a configured address; one daemon per PC; the watch aggregates. Omnara's `--no-tunnel` local mode shows demand for this.
- **Remote/SSH dev boxes:** a daemon on the box cannot reach the watch directly if the box is in a remote network. Use a small local aggregator or an outbound-only connection from the box to a user-run hub **[inferred]**. Keep a self-hosted relay as an option.
- **Prompt privacy on a wearable.** A watch face is visible to bystanders and screens can be seen over the shoulder. Default to metadata only (state, agent label); redact command text and message summaries until a wrist-raise or tap reveals them. Add a "private mode" toggle and a per-repo redaction rule (secrets, tokens, customer names). Never store content on the watch after the screen sleeps.
- **Transport security:** mutual auth between watch and daemon (pairing code or QR, as Happy does), TLS or Noise on the LAN, and per-device revocation. Happy's E2E encryption is a good reference.
- **Audit:** log every remote action (device ID, agent, request ID, decision, timestamp) on the host. This fits Copilot hook guidance ("audit logging for compliance") and Obsidian's point that skipping prompts also removes the audit trail. https://www.obsidiansecurity.com/academy/dangerously-skip-permissions-what-it-does-and-how-to-contain-it
- **Policy:** let admins disable remote approvals, or limit them to a tier, via a config file; the daemon must never override the CLI's own deny rules.
- **Lost device:** remote-wipe of pairing from the host; short-lived tokens.

## 6. Gaps and uncertainties
- No Reddit threads were retrieved; HN/blog coverage is only partial.
- Not researched in depth: Codex cloud, Jules, Devin notification features; VibeTunnel; ntfy/Pushover hook recipes; Copilot cloud agent attention flow.
- Omnara facts come from a third-party doc, not official docs.
- Token/cost and "last message" fields depend on what each CLI exposes in hooks; verify per CLI.
- Whether Copilot CLI permission prompts can be answered programmatically (beyond `preToolUse` and `--acp`) is unverified. #1651 mentions `--acp` as a workaround for the hook gap.

## Top 10 use cases for v1/v2/v3

**v1: Glance and route (read-only plus focus)**
1. **Needs-input alert with identity.** Haptic and color alert naming repo/branch/machine. This is the number-one pain.
2. **Attention queue / ring overview** across all agents and machines, with oldest-waiting first.
3. **Focus the agent's window on the right PC** with one gesture (the signature feature).
4. **"Notify me when done"** for long-running tasks, plus errored/failed alerts.
5. **Acknowledge and snooze** with auto-expiring snooze and escalation, to limit spam.

**v2: Safe control**
6. **Approve/deny with guardrails.** Full command text, deliberate gesture, risk tiers, request-ID binding; deny is easy, risky commands are "open on PC".
7. **Canned replies** ("continue", "yes", "stop and summarize", user-defined) and **pause/cancel**.
8. **Cost / context-window badge** with threshold alerts, best-effort per CLI.

**v3: Richer input and teams**
9. **Voice dictation** to a chosen agent, with a transcript confirmation step.
10. **Team/enterprise mode.** Audit log, admin policy, redaction rules, multi-user hub, self-hosted relay for SSH dev boxes and cloud agents.

## Anti-features to avoid
- **One-tap "approve all" / "always allow" / switching to bypass permissions** from the watch. It feeds the 93% rubber-stamp problem.
- **Approving without showing what is being approved.** No generic "Approve?" screens.
- **Cloud relay as a requirement.** Local-only must work; any cloud path is opt-in.
- **Screen-scraping as the primary state source.** It breaks with each CLI update; use hooks/structured events and treat scraping as a fallback.
- **Alert spam.** No notification for every tool call. Coalesce, rate-limit and let the user pick which states buzz.
- **Showing prompt or code content on the wake screen** by default.
- **Free-form typing or shell execution** on the watch.
- **Trying to be a full terminal or diff viewer.** A 466x466 display is for triage; send the user to the PC.
- **Stale approvals.** Never apply an approve to a prompt that has changed or timed out.
- **Vendor lock-in.** Do not build for one CLI only; the gap in the market is a multi-CLI, multi-machine view.
- **Silent failure.** If the watch loses contact with a daemon, show "stale/unknown", never a last-known "running".
