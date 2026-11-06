# Host integration research: agent status capture, window focus, transport, daemon, security

Scope: a cross-platform (macOS/Linux/Windows) host daemon that (a) observes AI coding-agent status and (b) focuses a specific agent's terminal window/tab/pane, driven by an M5Stack StopWatch (ESP32-S3, native USB-Serial/JTAG CDC VID 0x303a / PID 0x1001, Wi-Fi, BLE 5).

Evidence legend: **[V]** verified by fetching the doc or inspecting the local machine this session; **[M]** from background knowledge, not re-verified now (re-check before building). Local inspection was read-only; only key names and types were printed, no values or secrets.

---------------------------------------------------------------------------

## 0. Executive summary

* **Copilot CLI has first-class hooks** (`sessionStart`, `sessionEnd`, `userPromptSubmitted`, `preToolUse`, `postToolUse`, `agentStop`, `errorOccurred`, `notification`, `permissionRequest`, ...) including an `http` hook type that POSTs JSON. A `notification` hook with `notification_type = permission_prompt | elicitation_dialog | agent_idle | ...` is the ideal "needs attention" signal. Session files under `~/.copilot/session-state/<id>/events.jsonl` give a complete tail-able fallback and carry `permission.requested`, `assistant.turn_start/end`, `session.shutdown`, etc. [V]
* **Claude Code, Gemini CLI, and Codex CLI** all have equivalent hook or notify mechanisms (Claude: `Notification`/`Stop`/`PreToolUse`...; Gemini: `Notification`/`AfterAgent`...; Codex: `notify` argv program + TUI notifications). So one tiny `agentlink-hook` shim that forwards JSON to the local daemon covers all of them. [V for Claude/Gemini/Copilot docs, partial for Codex]
* **Focusing is the hard part**, and it is terminal-specific. The robust design is a **per-terminal "focus driver"** chosen from env vars captured by the hook shim at event time (`TERM_PROGRAM`, `ITERM_SESSION_ID`, `WEZTERM_PANE`, `KITTY_WINDOW_ID`, `TMUX`/`TMUX_PANE`, `WT_SESSION`, `VSCODE_*`, `WINDOWID`), plus PID/TTY ancestry as fallback. Wayland (GNOME in particular) can't be focused generically.
* **Transport recommendation:** USB CDC serial as the always-works baseline (COBS-framed CBOR or JSON lines), and BLE GATT (Nordic-UART-style custom service, watch = peripheral, up to N central connections) as the wireless/multi-PC path. Wi-Fi (watch as WebSocket/MQTT client to an mDNS-discovered daemon) is a third option; best when PCs are on the same LAN and many hosts are wanted.
* **Daemon language:** Rust is the best fit (single static binary, `serialport`, `btleplug`, `tray-icon`, `tao`/`winit`, native OS FFI for focus APIs). Go is the strongest runner-up for speed of development. Python is fine for a prototype (bleak, pyserial, psutil, pyobjc, pywin32) but packaging is worse.
* **Security:** treat the watch→PC channel as a remote-control channel. Pair explicitly (QR/PIN on the round screen), sign every command, and only allow **focus** by default. **Remote approval of permission prompts should be off by default** and, if offered, must be bound to a specific request id, tool, and command hash, with confirmation on the watch showing the command.

---------------------------------------------------------------------------

## 1. Agent status sources

### 1.1 GitHub Copilot CLI

#### 1.1.1 Hooks [V]
Source: https://docs.github.com/en/copilot/reference/hooks-reference

* Hooks are loaded from (policy → user → project → plugins) and **all are combined**:
  * Policy: `/etc/github-copilot/policy.d/*.json` (Linux/macOS, must be root-owned), `C:\ProgramData\GitHub\Copilot\policy.d\*.json` or `HKLM\Software\Policies\GitHub\Copilot` (Windows).
  * Repo: `.github/hooks/*.json`.
  * User: `~/.copilot/hooks/*.json` (`%USERPROFILE%\.copilot\hooks\` on Windows; `$COPILOT_HOME/hooks/` if set).
  * Inline `hooks` in `.github/copilot/settings.json`, `.github/copilot/settings.local.json`, or user `~/.copilot/settings.json`. (Doc states the CLI no longer reads `~/.copilot/config.json` for hooks.) `.claude/settings*.json` in the repo are also read.
  * Plugin `hooks.json`.
* **User-level `~/.copilot/hooks/agentlink.json` is the right install location** for a daemon: no repo changes, applies to every session on the machine.
* Config format (`version: 1`):

```json
{
  "version": 1,
  "hooks": {
    "sessionStart":  [{ "type": "command", "bash": "agentlink-hook copilot sessionStart",
                        "powershell": "agentlink-hook.exe copilot sessionStart", "timeoutSec": 5 }],
    "notification":  [{ "type": "command", "exec": "agentlink-hook", "args": ["copilot","notification"] }],
    "agentStop":     [{ "type": "http", "url": "https://localhost:PORT/hook" }]
  }
}
```

* Hook types: `command` (`bash`/`powershell`/`command`, or `exec`+`args` without a shell, CLI only), `http` (JSON POST; only `https://` unless `COPILOT_HOOK_ALLOW_LOCALHOST=1` is set, which permits `http://localhost`, `127.*`, `[::1]`; `preToolUse` and `permissionRequest` must be https), `prompt` (sessionStart only).
  Practical consequence: a **command hook with `exec`** avoids a shell and TLS entirely, which makes it the simplest choice. Default timeout 30 s, so keep the shim fast and fire-and-forget.
* Events (camelCase config → camelCase JSON; PascalCase config → VS Code-compatible snake_case JSON): `sessionStart`, `sessionEnd`, `userPromptSubmitted`, `userPromptTransformed`, `preToolUse`, `postToolUse`, `postToolUseFailure`, `permissionRequest`, `agentStop`, `subagentStart`, `subagentStop`, `errorOccurred`, `preCompact`, `notification`.
* Payload for lifecycle events includes `sessionId`, `timestamp`, `cwd` (+ `source`, `reason`). Subagent hooks add `agentId`, `agentType`, `parentAgentId`.
* **`notification` hook** (async, fire-and-forget, never blocks): payload `{sessionId, timestamp, cwd, hook_event_name:"Notification", message, title?, notification_type}`. Types: `shell_completed`, `shell_detached_completed`, `agent_completed`, `agent_idle`, `permission_prompt`, `elicitation_dialog`. This maps 1:1 to watch states: *needs approval* (`permission_prompt`), *needs input* (`elicitation_dialog`).
* **`permissionRequest` hook** can return `behavior: "allow" | "deny"` to short-circuit the permission flow. This is the (dangerous) mechanism that would enable approve-from-watch (see §5).
* `agentStop` = main agent finished a turn → "done/idle". `userPromptSubmitted` / `preToolUse` → "working".
* Hooks run in the same shell as the CLI → the hook process **inherits the terminal's environment** (`TERM_PROGRAM`, `WT_SESSION`, `ITERM_SESSION_ID`, `TMUX`, ...) and its **process tree parent chain leads to the terminal**. This is the key to focus mapping (§2.4). **[M/inferred]** that the env is inherited, consistent with "run on the developer's local machine in the same shell as the CLI" [V]; verify by dumping `env` from a test hook.
* Hooks are **not enabled for cloud agent in a useful way** for this project (sandbox, restricted network); ignore.

#### 1.1.2 Session state on disk [V, local machine inspection]
Layout observed at `~/.copilot/` (names only):
`config.json`, `settings.json` (keys: `voice, model, allowedUrls, showReasoning`), `mcp-config.json`, `permissions-config.json`, `open-sessions-state.json`, `sidebar-sessions-state/*.json`, `session-state/`, `session-store.db` (SQLite, tables incl. `sessions`, `turns`, `checkpoints`, `session_files`, `session_refs`, `search_index`), `hooks/` (absent by default), `installed-plugins/`, `logs/`, `ide/`, `servers/`.

`~/.copilot/session-state/<uuid>/` contains:
* `workspace.yaml` – `id, cwd, git_root, repository, host_type, branch, client_name (e.g. github/cli), name, summary_count, created_at, updated_at`.
* `events.jsonl` – one JSON object per line: `{type, data, id, timestamp, parentId}`.
* `inuse.<PID>.lock` and `inuse.<PID>.hold` – **the owning copilot process PID is encoded in the filename** (observed `inuse.29310.lock`; `ps` confirmed PID 29310 was the `copilot` process on tty `s003`). This gives a free session-id ↔ PID ↔ TTY mapping without hooks.
* `checkpoints/`, `files/`, `research/`, `rewind-file-snapshots/`.

`open-sessions-state.json`: map `sessionId → {schemaVersion, openedAt, refreshedAt, working: bool}`. A `working` boolean is directly usable as a coarse busy/idle signal. **[V structure; semantics inferred]**

`events.jsonl` event types seen across ~40 sessions (counts omitted): `session.start`, `session.resume`, `session.shutdown`, `session.error`, `session.warning`, `session.task_complete {summary, success}`, `session.mode_changed`, `session.model_change`, `session.compaction_start/complete`, `user.message`, `assistant.turn_start`, `assistant.turn_end {turnId, model}`, `assistant.message`, `tool.execution_start`, `tool.execution_complete`, `permission.requested`, `permission.completed`, `human_response.recorded {requestId, toolCallId, actor, response}`, `abort {reason}`, `subagent.started/completed`, `system.notification {content, kind{type, shellId, exitCode, description}}`, `model.*`, `session.usage_record`.
* `session.start.data` has `sessionId, copilotVersion, producer, context{cwd}, alreadyInUse, remoteSteerable`.
* `permission.requested.data` has `requestId, permissionRequest{kind, toolCallId, fullCommandText, shellType, intention, commands, possiblePaths, possibleUrls, canOfferSessionApproval, ...}`; matched by `permission.completed`. **State machine from the log:**
  * `user.message` / `assistant.turn_start` → WORKING
  * `permission.requested` (no matching `permission.completed`) → NEEDS_APPROVAL
  * `assistant.turn_end` + `session.task_complete` → DONE/IDLE
  * `abort` → IDLE (aborted); `session.error` → ERROR; `session.shutdown` → GONE
* Caveat: the file format is **internal and undocumented**, unversioned per event, and may change. Use hooks as primary; use the tail as a fallback/cross-check and for sessions started before the hook was installed. Watching is cheap: `notify` (Rust) / `fsnotify` (Go) / `watchdog` (Python) on `session-state/*/events.jsonl`, reading only appended bytes.
* **Privacy:** `events.jsonl` contains prompts, full command text, and file contents. The daemon must send only derived status plus a short, truncated label (repo name, branch) to the watch; never raw events.

#### 1.1.3 MCP mechanism
* `~/.copilot/mcp-config.json` configures MCP servers. An MCP server is a tool *provider* that the model calls; it can't passively observe agent state, and relying on the model to call a "report_status" tool is unreliable. **Not recommended as a status source.** It can be useful for the reverse direction (watch → agent: e.g. a "what's the user's quick reply" tool) but that increases the injection surface (§5).
* `remoteSteerable` in `session.start` and `client_name` fields suggest a remote-control feature exists in the CLI **[inferred; not documented in what was fetched; follow up in https://github.com/github/copilot-cli]**.

#### 1.1.4 Other
* The `github/copilot-cli` repo docs and `copilot help` document config dir layout (https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-config-dir-reference [referenced by the hooks page, V]). The `COPILOT_HOME` env var relocates `~/.copilot`.
* Terminal bell / OSC: not verified for Copilot CLI; do not depend on it.

### 1.2 Claude Code [V: https://code.claude.com/docs/en/hooks]
* Hook events: `SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PermissionRequest`, `PermissionDenied`, `PostToolUse`, `PostToolUseFailure`, `Notification`, `SubagentStart/Stop`, `Stop`, `StopFailure`, `TeammateIdle`, `PreCompact`, `SessionEnd`, `CwdChanged`, etc.
* Hook handler types: command (JSON on stdin), HTTP (POST), MCP tool, prompt, agent.
* Configured in `~/.claude/settings.json` (user), `.claude/settings.json` / `.claude/settings.local.json` (project). Common input includes `session_id`, `transcript_path`, `cwd`, `hook_event_name` **[M]**.
* Mapping: `Notification` (permission prompt / idle ≥60 s) → needs attention; `Stop` → done; `UserPromptSubmit`/`PreToolUse` → working; `SessionEnd` → gone.
* Transcripts at `~/.claude/projects/<encoded-cwd>/<session>.jsonl` **[M]** as a fallback.

### 1.3 OpenAI Codex CLI
* `notify = ["program", "args"]` in `~/.codex/config.toml`: Codex runs the program with a JSON argument (event `agent-turn-complete` with thread id, cwd, last assistant message) **[M]**. Docs: https://developers.openai.com/codex/config-advanced (page contains a `notify` section [V that it exists; payload details M]). Only "turn complete" is delivered through `notify` historically; newer builds also have TUI notifications (`tui.notifications`) using OSC 9 **[M]**; approval-requested events may be available there **[M, verify]**.
* Fallback: session rollouts under `~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl` **[M]**; tail them like Copilot's `events.jsonl`.
* Codex has also introduced a hooks system similar to Claude's in newer versions **[M, verify]**.

### 1.4 Gemini CLI [V: https://github.com/google-gemini/gemini-cli/blob/main/docs/hooks/reference.md]
* Events: `BeforeTool`, `AfterTool`, `BeforeAgent`, `AfterAgent`, `BeforeModel`, `BeforeToolSelection`, `AfterModel`, `SessionStart`, `SessionEnd`, `Notification` (`notification_type: "ToolPermission"`), `PreCompress`. Configured in `settings.json` (`~/.gemini/settings.json`); command hooks get JSON on stdin.
* Mapping: `Notification/ToolPermission` → needs approval; `AfterAgent` → done; `BeforeAgent` → working.

### 1.5 Aider [M]
* No hooks. Options: `--notifications` + `--notifications-command` (runs a command when the LLM finishes and awaits input) **[M, verify in https://aider.chat/docs/config/options.html]**; `.aider.chat.history.md` and `.aider.input.history` in cwd as logs; otherwise process inspection. Status granularity is just "waiting for input".

### 1.6 VS Code Copilot agent mode [M]
* VS Code supports agent hooks in the PascalCase/snake_case "VS Code compatible" format (same format the CLI accepts, confirmed by the hooks page [V]). Docs: https://code.visualstudio.com/docs/copilot/customization/hooks **[M URL]**.
* Chat sessions persist in VS Code's workspaceStorage (`state.vscdb` SQLite / `chatSessions/*.json`) **[M]**, an unstable format.
* Best: a tiny **companion VS Code extension** that (1) reports state from the `vscode.chat`/extension APIs available (limited) and (2) exposes a `vscode://agentlink.focus?...` URI handler to focus the window/terminal (§2). Hooks supply status.

### 1.7 Generic fallbacks (any agent, any terminal)
| Method | What it gives | Limits |
|---|---|---|
| **Process inspection** (`ps`, `psutil`, `sysinfo` crate, `gopsutil`) | Detect agent processes by name (`copilot`, `claude`, `codex`, `gemini`, `aider`), cwd, tty, parent chain, CPU activity (busy vs idle) | No semantic state; CPU heuristic is noisy; macOS cwd of other processes needs `proc_pidinfo`/`lsof` (works for same user) |
| **Terminal bell (BEL, 0x07)** | Terminals raise an attention flag, tab badge/dock bounce | Not readable from outside the terminal without a terminal API; wezterm/kitty/iTerm2 can expose it as an event |
| **OSC 9 / OSC 777;notify / OSC 99 (kitty)** | Desktop notification text from the app | Terminal consumes it; observers need to be the terminal or a PTY proxy (wrap the agent in `agentlink run -- copilot`, which owns the PTY and can see the OSC bytes). OSC 9;4 is a progress sequence (Windows Terminal) **[M]** |
| **PTY wrapper** (`agentlink run`) | Sees all I/O, can detect prompts ("Allow? (y/n)") and idle time, exact tty/pid | Intrusive; users must launch via wrapper; breaks some TUIs unless implemented carefully |
| **tmux** `tmux list-panes -a -F '#{pane_id} #{pane_pid} #{pane_tty} #{pane_current_command} #{pane_current_path} #{window_activity} #{window_bell_flag}'`; `tmux -C` control mode streams `%output`, `%window-add`, bell events | Complete pane inventory + bell flag; `tmux select-window -t` / `select-pane -t` | Only inside tmux; still need to focus the *outer* terminal window |
| **zellij** `zellij action ...`, `zellij list-sessions`; plugins can subscribe to pane events **[M]** | Focus panes in a session | Fewer introspection hooks than tmux |
| **WezTerm** `wezterm cli list --format json` (pane_id, tty_name, cwd, title, window_id, tab_id), `wezterm cli activate-pane --pane-id N`, `activate-tab`, Lua events (`bell`, `update-status`) | Full inventory with TTY → pane mapping | https://wezterm.org/cli/cli/index.html [M URL] |
| **kitty** `kitten @ ls` (JSON: windows with `pid`, `cwd`, `foreground_processes`, `env`), `kitten @ focus-window --match id:N`, `kitten @ focus-tab`; requires `allow_remote_control` and a listen socket (`--listen-on`) | Good PID matching | https://sw.kovidgoyal.net/kitty/remote-control/ [M URL] |
| **iTerm2** Python API (websocket over unix socket, needs Python API enabled) or AppleScript: `tell session id ... select`; `ITERM_SESSION_ID=w0t1p0:UUID` env | Session ids, `tty`, badge, trigger on regex ("needs approval") | https://iterm2.com/documentation-scripting.html [M URL] |

---------------------------------------------------------------------------

## 2. Focusing a window / tab / pane per OS

### 2.0 Strategy: two-level focus
1. **Pane/tab selection inside the terminal** using the terminal's own API (tmux/WezTerm/kitty/iTerm2/Windows Terminal).
2. **Bring the OS window forward** (activate app / raise window).
Do (1) first, then (2). Many terminals' APIs (`wezterm cli activate-pane`, `kitten @ focus-window`) only change the focus *within* the app and do not raise the app → still need (2).

### 2.1 macOS
* **App activation:** `NSRunningApplication(processIdentifier:).activate(options: .activateAllWindows)` (macOS 14 changed to `activate()`, and cooperative activation rules: an app can't be force-activated by another unless the user is interacting; `open -a`/AppleScript `activate` still works in practice **[M]**). Alternative: `open -b com.apple.Terminal`.
* **Terminal.app** (AppleScript): windows/tabs expose `tty`. Focus by tty:
  ```applescript
  tell application "Terminal"
    repeat with w in windows
      repeat with t in tabs of w
        if tty of t is "/dev/ttys003" then
          set selected of t to true
          set index of w to 1
          activate
        end if
      end repeat
    end repeat
  end tell
  ```
  The first AppleScript call from the daemon triggers an **Automation (Apple Events/TCC) permission prompt** per target app. `NSAppleEventsUsageDescription` is needed when the daemon is a signed app bundle.
* **iTerm2:** AppleScript `tell application "iTerm2" ... repeat with s in sessions of t ... if tty of s is ... then select s`; or Python API `iterm2.Session.async_activate()` using `ITERM_SESSION_ID` (format `w0t0p0:GUID`; the GUID is the session id) **[M]**. Needs "Enable Python API" in settings.
* **Ghostty:** has macOS AppleScript support in newer versions **[M, verify]** (1.3 added scripting dictionary), and `ghostty +...` CLI actions are limited. Fallback: activate the app + Accessibility window title match.
* **WezTerm:** `wezterm cli activate-pane --pane-id $WEZTERM_PANE` (+ activate app by bundle id `com.github.wez.wezterm`). `WEZTERM_PANE` and `WEZTERM_UNIX_SOCKET` env vars give the target.
* **kitty:** `kitten @ --to unix:$KITTY_LISTEN_ON focus-window --match id:$KITTY_WINDOW_ID` + activate app.
* **tmux:** `tmux select-window -t $TMUX_PANE; tmux select-pane -t $TMUX_PANE` then find the client terminal (`tmux list-clients -F '#{client_tty} #{client_pid}'`) and focus that terminal via the above.
* **VS Code integrated terminal:** `TERM_PROGRAM=vscode`, `VSCODE_IPC_HOOK_CLI` (a unix socket for the window, usable by `code` CLI), `VSCODE_GIT_ASKPASS_*`. Options: (a) companion extension with `vscode.window.registerUriHandler` for `vscode://agentlink.agentlink/focus?pid=...` which finds the `Terminal` whose `processId` matches (`vscode.window.terminals[i].processId` is a Promise<number>) and calls `terminal.show()`; `open vscode://...` raises the correct VS Code window (the URI goes to the last active window, so also pass the folder; or use `code --reuse-window <folder>`); (b) simpler: `code -r <cwd>` raises the window for the folder but not the terminal. **[M]**
* **Accessibility fallback (any app):** AXUIElement (`kAXRaiseAction`, `AXMain`, `AXFocused`) to raise a specific window by title; requires **Accessibility permission** (System Settings → Privacy & Security → Accessibility) for the daemon binary; grant is tied to code signature/path, so sign the binary or the grant is lost on each rebuild. Enumerate windows with `CGWindowListCopyWindowInfo` (no permission for names of own-user windows except Screen Recording for titles on macOS 10.15+) **[M]**.
* Tooling: AppleScript/JXA via `osascript`; Rust `objc2` crates, Swift helper, or Python `pyobjc`. Hammerspoon (`hs.window.filter`) is a popular existing implementation of this idea.

### 2.2 Windows
* **Windows Terminal:** env `WT_SESSION` (GUID of the pane/tab) and `WT_PROFILE_ID`. `wt.exe -w 0 focus-tab -t N` focuses by *index* in a window; `wt -w <window-id-or-name> focus-tab` and `move-focus` exist. There is **no documented "focus by WT_SESSION GUID"** API **[M]**; workable approach: map PID → console/pseudo-console → find `WindowsTerminal.exe` ancestor → raise the top-level HWND (`SetForegroundWindow`), and use UI Automation to select the tab whose name matches the title (tab `TabItem` elements expose `SelectionItemPattern.Select()`). Window naming (`wt -w mywin`) at launch makes `wt -w mywin focus-tab -t N` scriptable.
  Docs: https://learn.microsoft.com/en-us/windows/terminal/command-line-arguments [M URL].
* **Finding the window for a PID:** walk the parent chain (`CreateToolhelp32Snapshot` / psutil) from the agent PID to the first process owning a visible top-level window (`EnumWindows` + `GetWindowThreadProcessId`). For conhost-hosted consoles `GetConsoleWindow()` works only in the same console (use `AttachConsole`); for Windows Terminal the HWND belongs to `WindowsTerminal.exe` (the shell is a child of `OpenConsole.exe`, whose parent is `WindowsTerminal.exe`) **[M]**.
* **SetForegroundWindow restrictions** (https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow, [M URL]): only allowed if the calling process is the foreground process, received the last input event, or is attached to the foreground thread, etc. Workarounds:
  * `AllowSetForegroundWindow(ASFW_ANY)`/`LockSetForegroundWindow` unlocking is not available to background daemons.
  * Send a synthetic **Alt key press** (`keybd_event(VK_MENU)`) before calling `SetForegroundWindow` (the classic hack: the process "received the last input event").
  * `AttachThreadInput(foregroundThread, ourThread, TRUE)` then `SetForegroundWindow`, then detach.
  * `ShowWindow(SW_RESTORE)` for minimized windows first; `BringWindowToTop`; `SwitchToThisWindow` (undocumented).
  * Registry `ForegroundLockTimeout=0` (global, discouraged).
  * Since the user just touched the watch (hardware input, not Windows input), Windows won't count it as user input; expect the taskbar button to **flash** instead of focusing when all else fails. Best UX: run the daemon with a user-session helper process and use the Alt trick or UIAutomation `SetFocus()`.
* **UI Automation (UIA):** `IUIAutomation::ElementFromHandle`, `FindFirst(TabItem by Name)`, `SelectionItemPattern.Select()`; Rust `windows` crate (`windows::Win32::UI::Accessibility`), Python `uiautomation`/`pywinauto`, .NET `System.Windows.Automation`.
* **VS Code:** same as macOS (extension + URI handler). 
* **WSL:** agents running inside WSL expose `WT_SESSION` and `WSL_DISTRO_NAME`; hook shim should be a Windows exe invoked via interop or talk over localhost (WSL2 mirrored networking / `localhost` forwarding).

### 2.3 Linux
* **X11:** `wmctrl -i -a <hex-window-id>` or `xdotool windowactivate <id>`; map PID → window via `_NET_WM_PID` (`xprop`, `xdotool search --pid`), **`$WINDOWID`** (set by xterm/many VTE/kitty/Alacritty terminals) is the simplest reliable id. Programmatic: `x11rb`/`xcb`, send `_NET_ACTIVE_WINDOW` client message. Terminals with multiple tabs sharing one PID (gnome-terminal) can't be tab-selected from X11; use the tmux layer or terminal APIs.
* **Wayland** forbids clients from stealing focus or enumerating other clients' windows; compositor-specific paths:
  * **GNOME/Mutter:** `xdg-activation` token requires prior user interaction; `gdbus call org.gnome.Shell.Eval` is restricted since 41/42 (disabled unless unsafe mode). Options: a **GNOME Shell extension** exposing a D-Bus method `Focus(window_id)` (this is how "Window Calls" / "Focused Window D-Bus" extensions work), or `window-calls` extension via `gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell/Extensions/Windows --method org.gnome.Shell.Extensions.Windows.Activate <id>` **[M]**. Needs the user to install the extension.
  * **KDE Plasma/KWin:** `kdotool` (https://github.com/jinliu/kdotool) loads KWin scripts over D-Bus to `activate`/search windows by pid/class **[M]**; or `qdbus org.kde.KWin /KWin ...` scripting.
  * **sway:** `swaymsg '[pid=1234] focus'`, `swaymsg -t get_tree` (includes `pid`, `app_id`). **Hyprland:** `hyprctl clients -j`, `hyprctl dispatch focuswindow pid:1234`. **i3:** `i3-msg '[id=...] focus'`. All **[M]**, stable and well-documented.
  * **wlroots generic:** `wlr-foreign-toplevel-management` protocol (activate toplevel) **[M]**.
  * Other compositors / GNOME without the extension: best effort = **notify only** (post a desktop notification with a click action) or fall back to the terminal's own control API (WezTerm/kitty can't raise their own window under Wayland either unless xdg-activation tokens are used).
* **tmux:** `tmux select-window`/`select-pane`, works everywhere and is the only fully portable Linux path.
* `XDG_SESSION_TYPE` (`x11`/`wayland`) tells which branch to take.

### 2.4 Mapping an agent process to its window/tab/pane
Capture at **hook time** (in the shim, which is a descendant of the agent) a "focus handle" and send it with every event; this beats retroactive process-tree guessing:

```json
{ "agent":"copilot", "session_id":"...", "pid":29310, "ppid_chain":[...], "tty":"/dev/ttys003",
  "cwd":"/path", "host":"macbook",
  "handle": { "term_program":"iTerm.app", "iterm_session":"w0t1p0:GUID", "wezterm_pane":"12",
              "kitty_window":"3", "tmux":"/tmp/tmux-501/default,123,0", "tmux_pane":"%4",
              "wt_session":"GUID", "windowid":"0x4a00007", "vscode_ipc":"/..." } }
```

Resolution order in the daemon:
1. tmux (`TMUX_PANE`) → select pane, then resolve the client terminal and continue below.
2. Terminal-native id env (`WEZTERM_PANE`, `KITTY_WINDOW_ID`, `ITERM_SESSION_ID`, `WT_SESSION`, `WINDOWID`) → terminal API.
3. TTY match: macOS Terminal/iTerm2 AppleScript `tty`; WezTerm `tty_name`; Linux `/proc/<pid>/stat` tty_nr.
4. PID ancestry: walk `ppid` up (psutil `parents()`, `sysinfo`, `/proc`, `proc_pidinfo`) to the first process that owns a GUI window (match to window list by PID: `_NET_WM_PID`, `CGWindowListCopyWindowInfo` `kCGWindowOwnerPID`, `EnumWindows`).
5. cwd/title match (last resort; for VS Code the folder).
Copilot-specific: `inuse.<PID>.lock` in the session dir maps session id → agent PID even when no hook has fired yet (§1.1.2).

---------------------------------------------------------------------------

## 3. Transport between daemon and watch

### 3.0 Requirements
* Low rate (status ≈ a few events/s, small payload), tiny MCU parser, multi-host, optional encryption/auth, reconnection, power-friendly.
* Direction: PC → watch (status, session list with labels/colors/state), watch → PC (focus session X, optional approve/deny, acknowledgements).

### 3.1 USB CDC serial (ESP32-S3 USB-Serial/JTAG)
* The device enumerates as VID `0x303a` PID `0x1001` (Espressif USB JTAG/serial debug unit); the daemon finds it by VID/PID via `serialport::available_ports()` (`UsbPortInfo{vid,pid,serial_number}`). Caveat: the same port is the console/`esptool` flashing port, and DTR/RTS toggling can **reset the chip into bootloader** (the USB-Serial/JTAG peripheral decodes DTR/RTS patterns). Open the port with DTR/RTS explicitly de-asserted (or use `usb-serial-jtag` driver config `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` and keep logs off the protocol channel) **[M; Espressif docs: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html]**. Alternative: TinyUSB CDC as a separate composite interface (different PID).
* Single link per USB cable → inherently one PC at a time (unless using a USB hub with host-switching—irrelevant). USB also powers/charges the watch.
* **Framing options:**
  * **COBS** (Consistent Overhead Byte Stuffing) with `0x00` delimiter + CRC16/32: robust resync after reset, binary-safe. Libraries: `cobs` crate, `postcard` (Rust, serde, COBS built in, used heavily on embedded), `PacketSerial` (Arduino), `nanocobs`.
  * **CBOR** (compact, schema-less; `minicbor`, `TinyCBOR`, `QCBOR`) or **protobuf/nanopb** (schema, codegen, versioned fields) or **MessagePack**.
  * **JSON Lines** (`\n`-terminated; trivially debuggable with `screen`/`jq`; ArduinoJson on ESP32). With status messages of ~100 B this is entirely adequate; interleaved ESP-IDF log lines must be distinguished by a prefix or by `{` start.
  * SLIP (RFC 1055) is similar to COBS; mcumgr/SMP, MCUboot use CBOR over serial with base64+CRC framing.
* Projects doing this: **Home Assistant ESPHome** (native API is protobuf over TCP, also serial logging), **Klipper** (custom framed serial protocol over USB CDC), **Meshtastic** (protobuf over serial/BLE/TCP with `0x94 0xc3` length-prefix framing: https://meshtastic.org/docs/development/device/client-api/), **Zephyr mcumgr/SMP** (CBOR over serial/BLE), **Elgato/Stream Deck-like devices** (HID), **QMK Raw HID**.
* Recommendation: **COBS + CBOR (or JSON-in-COBS during bring-up)** with `{ver, seq, type}` envelope; keep a text-only debug mode.

### 3.2 BLE GATT
* ESP32-S3 supports BLE 5 (NimBLE or Bluedroid; NimBLE is lighter). Watch is the **peripheral/GATT server**; daemons are **centrals/clients**.
* Custom service pattern: **Nordic UART Service (NUS)** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` with RX (write/write-without-response) `…0002…` and TX (notify) `…0003…`: super common, supported by every library and by tools like nRF Connect for debugging. Alternative: a **custom 128-bit service** with separate characteristics: `status` (notify), `command` (write), `sessions` (read/indicate), `auth` (write), giving typed access and easier versioning. A message-oriented design on NUS needs its own framing (reuse COBS/CBOR) because ATT MTU (23 default, up to ~247 after MTU exchange; `bleak`/`btleplug` negotiate automatically on most OSes) limits chunk size.
* **Bonding/LE Secure Connections:** use LESC with *Passkey Entry* (watch displays 6-digit number on the round screen, user types it on the PC OS pairing dialog) or *Numeric Comparison* (both show; confirm on watch touch) → MITM-protected link, encrypted, bonded keys stored in NVS. Mark characteristics as `ENC`/`AUTHEN` so unauthenticated centrals can't write. **Note:** OS pairing dialogs are OS-specific and poorly scriptable; Windows/macOS/Linux BlueZ differ; btleplug/bleak can't drive bonding on every platform (bleak supports `pair()` on Windows/BlueZ; on macOS pairing is triggered implicitly by accessing a protected characteristic). A more portable alternative is **application-layer pairing** over an unencrypted-but-unprivileged channel (see §5).
* **Multi-host:** the ESP32-S3 NimBLE stack supports multiple simultaneous connections (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, default 3, up to ~9 with RAM costs). Each PC bonds once (bond table in NVS), device advertises as connectable; each daemon reconnects (autoconnect with whitelist). The watch aggregates sessions from all hosts, tagging each with `host_id`, and routes touch commands by `host_id`. Advertise a **service UUID** (and short name) so scanners can filter. Windows needs `GattSession.MaintainConnection`; macOS CoreBluetooth hides MAC addresses (use peripheral UUID + stored identity) **[M]**.
* Latency/power: connection interval 30–100 ms is ample; notifications avoid polling.
* Libraries/projects: **Nordic UART** (everywhere), **ESP32 BLE Keyboard/Gamepad**, **Pebble/Garmin companion apps** (phone as central), **Gadgetbridge** (Android app ↔ watches over custom GATT), **InfiniTime/PineTime** (NimBLE, custom services; companion apps like Gadgetbridge/Amazfish), **Amazfish**, **Meshtastic BLE** (protobuf over a single "toRadio/fromRadio" characteristic pair) **[M]**.
* Cons: pairing UX across 3 OSes, background BLE permissions (macOS needs `NSBluetoothAlwaysUsageDescription` and TCC Bluetooth prompt for a bundled app; Linux needs BlueZ ≥5.5x and user in `bluetooth` group; Windows needs a packaged/unpackaged app with Bluetooth capability for some APIs).

### 3.3 Wi-Fi
* **Discovery:** mDNS/DNS-SD. Daemon advertises `_agentlink._tcp.local` (txt: `ver`, `host_id`, `fp=<pubkey fingerprint>`); the watch browses (ESP-IDF `mdns` component supports query/browse) **or** the watch advertises and daemons connect in. Direction choice: **watch connects out to daemons** makes it easy for N PCs (each PC is a server) but needs inbound firewall rules on PCs (Windows firewall prompt; macOS "accept incoming connections" prompt); **daemons connect in to the watch** (watch runs a small WebSocket/TCP server) needs no PC firewall changes but the watch holds N sockets and must be discoverable. Corporate/guest networks often block mDNS & client-to-client traffic (AP isolation) → Wi-Fi works best at home; document fallback to USB.
* **Protocols:** WebSocket (`esp_websocket_client`, `ESPAsyncWebServer`) with JSON/CBOR frames; **MQTT** (ESP-MQTT, broker e.g. Mosquitto/embedded `rumqttd`; topics `agentlink/<host>/status`, `agentlink/<host>/cmd`) naturally supports multi-host fan-in, retained "last status", and LWT (last-will for offline detection); raw TCP + COBS also works. **ESPHome native API (protobuf over TCP with Noise encryption)** is a good precedent: Noise_NNpsk0 with pre-shared key, plus mDNS discovery by Home Assistant.
* **TLS:** ESP-IDF mbedTLS; use either self-signed cert pinned at pairing (store SHA-256 fingerprint) or **Noise protocol** (`snow` crate / `noise-c`) with a PSK or static keys exchanged during pairing; avoids CA/cert-expiry hassle on a LAN device. Copilot's own `http` hook requires https (or localhost) [V] but that is the hook→daemon hop, separate from daemon→watch.
* **Pairing UX for Wi-Fi:** on-screen **QR code** on the round display encoding `{host_id, pubkey, otp, ip?}` that the daemon CLI/tray scans (needs a camera) or, better, the daemon shows a QR/PIN and the watch scans/types? The watch has no camera, so: **watch shows 6-digit PIN (or QR) → user enters in daemon tray dialog** → PAKE (SPAKE2/OPAQUE-style, or simply HKDF over PIN with SAS confirmation) derives keys; or watch shows a QR containing its public key fingerprint + one-time secret, the PC app (camera/ paste) consumes it. Provisioning Wi-Fi credentials onto the watch can use ESP-IDF **BLE/SoftAP provisioning** (`wifi_provisioning`) or via the USB serial channel.
* Projects: **ESPHome**, **Tasmota** (MQTT), **WLED** (WebSocket + mDNS), **Home Assistant** (MQTT discovery), **Moonraker/Klipper** (WebSocket JSON-RPC), **ntfy**/Pushover for notification-only.

### 3.4 One watch, multiple PCs at once
| Approach | How | Notes |
|---|---|---|
| **BLE multi-central** | Watch peripheral, each PC daemon a central; each message tagged `host_id`; watch keeps per-host session lists | Up to ~3–4 reliable links; each daemon bonds separately. Good wireless story, no infra |
| **Wi-Fi (watch as client)** | Each PC daemon exposes `wss://host:port`, advertised via mDNS; watch connects to all paired hosts (or MQTT broker) | Scales to many hosts, works while charging on a desk; LAN-only; needs firewall rule |
| **Wi-Fi via relay/broker** | All daemons publish to a local or cloud broker (MQTT/ntfy); watch subscribes | Off-LAN reach, but adds third-party trust & latency; commands must be end-to-end signed (§5) |
| **USB** | One host at a time | Always-works fallback, also power; when attached, USB host is "primary" |
| **Hybrid** | USB when plugged in, else BLE, else Wi-Fi; daemon implements a `Transport` trait with the same message protocol | Recommended. Protocol is transport-agnostic (same envelope/CBOR) |

Watch-side data model: `hosts[host_id] → {name, transport, last_seen, sessions[session_id → {agent, state, label, since}]}`; UI sorts globally by urgency (needs-approval > error > done > working). Commands carry `(host_id, session_id, focus_handle_id)` — the **daemon owns handles**; the watch only stores opaque ids (so the watch can't be used to run arbitrary commands).

---------------------------------------------------------------------------

## 4. Daemon implementation and packaging

### 4.1 Language comparison
| | Rust | Go | Python | Node/Electron |
|---|---|---|---|---|
| Single binary / small | Yes (5–15 MB) | Yes (10–20 MB) | PyInstaller/Nuitka (30–80 MB, AV false positives on Windows) | Bundled runtime (50–200 MB) |
| Serial | `serialport` (https://github.com/serialport/serialport-rs), `tokio-serial` | `go.bug.st/serial` (https://github.com/bugst/go-serial) | `pyserial` | `serialport` (native addon) |
| BLE | `btleplug` (https://github.com/deviceplug/btleplug; Win/macOS/Linux/iOS/Android; central role only), `bluer` (Linux BlueZ, incl. peripheral) | `tinygo.org/x/bluetooth` (https://github.com/tinygo-org/bluetooth; works on desktop macOS/Linux/Windows as central or peripheral, not only TinyGo targets), `muka/go-bluetooth` (Linux) | `bleak` (https://github.com/hbldh/bleak; Win/macOS/Linux, very mature) | `@abandonware/noble` (maintenance issues) |
| Tray | `tray-icon` + `tao`/`winit` (Tauri team), `ksni` (Linux SNI) | `fyne.io/systray`, `getlantern/systray` | `pystray` | Electron `Tray` |
| OS APIs for focus | `objc2`, `windows` crate, `x11rb`, `zbus` | `golang.org/x/sys`, `lxn/win`, `go-ole`, cgo for macOS (annoying) | `pyobjc`, `pywin32`, `python-xlib`, `psutil` | native addons (painful) |
| Proc inspection | `sysinfo` | `gopsutil` | `psutil` | `ps-list` |
| Cross-compile | OK (cross, cargo-zigbuild) but macOS BLE/AppKit needs macOS runner | Easy except cgo | N/A | N/A |
| Footprint/idle | tiny | tiny | moderate | large |
| Dev speed | slower | fast | fastest | fast |

**Recommendation: Rust core daemon** with thin per-OS focus modules (and shell-outs to `osascript`, `wezterm`, `kitten`, `tmux`, `swaymsg`, `hyprctl`, `wmctrl` where that's the most stable API), `serialport` + `btleplug`, `notify` for file watching, `sysinfo` for processes, `ciborium`/`minicbor` + `cobs`. If team velocity matters more: Go (`go.bug.st/serial` + `tinygo bluetooth`) is nearly as good. Python is ideal for the first spike (bleak + pyserial + psutil + AppleScript) and could be swapped later because the protocol is transport-agnostic.

Component split (keeps privileges tight):
* `agentlinkd` – long-running per-user daemon: transport manager, state aggregator, focus dispatcher, localhost IPC server (unix socket / named pipe) for hooks.
* `agentlink-hook` – ~100 KB shim called by agent hooks; reads stdin JSON + env + ppid chain; connects to daemon IPC (unix socket `$XDG_RUNTIME_DIR/agentlink.sock`, `~/Library/Application Support/...`, Windows named pipe `\\.\pipe\agentlink`), **fails silently and quickly** if the daemon is down (never block the agent; Copilot hook timeout default 30 s [V]).
* `agentlink-vscode` (optional extension) – focus/URI handler, terminal pid mapping.
* Optional GNOME-shell extension for Wayland.

### 4.2 Autostart
* **macOS:** per-user LaunchAgent `~/Library/LaunchAgents/dev.agentlink.plist` (`RunAtLoad`, `KeepAlive`) — must be a **LaunchAgent (GUI session)**, not a LaunchDaemon, so it can talk to WindowServer/AppleScript. For TCC grants, wrap in a signed `.app` bundle (`LSUIElement=true` for no Dock icon), use `SMAppService` (macOS 13+) for login item registration. Notarize to avoid Gatekeeper trouble.
* **Linux:** `systemctl --user` unit (`~/.config/systemd/user/agentlinkd.service`, `WantedBy=graphical-session.target`, needs `DISPLAY`/`WAYLAND_DISPLAY` imported: `systemctl --user import-environment`) or XDG autostart `.desktop`. Add a udev rule for the watch serial device (`SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", TAG+="uaccess"`) and membership in `dialout`/`uucp` otherwise.
* **Windows:** Task Scheduler "At log on" task (`schtasks /create /sc onlogon /tn AgentLink /tr ...` run as the interactive user, **not** SYSTEM, so the daemon is in the user's desktop session and can focus windows) or `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. A Windows Service runs in session 0 and **cannot** show/focus windows — avoid.
* Installers: `cargo-dist`, `cargo-bundle`, WiX/MSIX (Windows), `.pkg`/`.dmg` + Homebrew cask (macOS), `.deb`/`.rpm`/AUR/Flatpak (Flatpak sandbox complicates serial/BLE/focus; prefer native packages).
* The installer/`agentlink install` command should write the Copilot user hook file `~/.copilot/hooks/agentlink.json` and equivalents for Claude (`~/.claude/settings.json` merge), Gemini (`~/.gemini/settings.json` merge), Codex (`~/.codex/config.toml` `notify`). Merge, don't overwrite; keep an uninstall.

---------------------------------------------------------------------------

## 5. Security

### 5.1 Threat model
Assets: the user's PC session (anything focus/approve can do), prompts/code that flow to the watch, agent permissions. Attackers: a nearby BLE device, another LAN host, a malicious process on the same PC (including a prompt-injected agent), a stolen/lost watch, a malicious repo that ships `.github/hooks` (note: repo hooks run arbitrary commands, which is why Copilot gates them with folder trust [V "Repository ... hooks require workspace admission"]).

### 5.2 Pairing and authentication
* **Pair once, per host:** the watch shows a **6-digit PIN or QR** on the round screen; the daemon's tray/CLI prompts for it. Run an authenticated key exchange (SPAKE2/PAKE or ECDH with SAS comparison, or BLE LESC passkey), then persist a per-host **long-term key** (daemon: OS keychain/Keychain/DPAPI/Secret Service; watch: NVS with flash encryption enabled). Prefer a pairing window that is **opened by physical action** (long-press on watch + explicit "pair" in tray) and expires after ~60 s.
* **Every command watch→PC is authenticated:** AEAD (ChaCha20-Poly1305 via Noise `snow`, or HMAC-SHA256) over `{host_id, counter/nonce, ts, session_id, action}`; reject replays (monotonic counter), stale timestamps, unknown sessions. Revocation list on both sides.
* Transport security is **not** a substitute: BLE LESC protects the link but any app on the paired PC could still talk to the characteristic via the OS; USB serial is accessible to any local process of that user (OS device permissions only). So do app-layer auth even on USB (cheap).
* **Local IPC hook → daemon:** unix socket with `0600` perms in a per-user dir / Windows named pipe with an ACL for the current user only; never a TCP port on `0.0.0.0`. If using Copilot's `http` hook, bind to `127.0.0.1` with a random per-install token in a header (`headers` + `allowedEnvVars` [V]) and note `COPILOT_HOOK_ALLOW_LOCALHOST=1` is required for non-TLS. Prefer `exec` command hooks → unix socket. Hook input is untrusted-ish text (prompts, commands) → treat as data; cap size; never shell out with it.
* **Focus commands are low risk** (worst case: attention-stealing); limit by rate, and require that `session_id` exists in the daemon's own table (the watch can't specify arbitrary window ids/commands). Focus handles are looked up locally, never executed from watch-supplied strings (no `osascript` injection: pass tty/ids as validated tokens).
* **Data minimization:** send labels (repo, branch, state, a ≤40-char summary), not code or full commands unless the user explicitly enables it; BLE advertisements should not contain identifying info (use rotating/resolvable private addresses).
* **Lost watch:** the daemon can "forget" a device; watch can be wiped (NVS erase); keys per host so one compromise doesn't spread. Optional on-watch lock/PIN.
* **Firmware/updates:** signed OTA (ESP-IDF secure boot v2) if distributed.

### 5.3 Why remote approval of permission prompts is risky
1. **Blind approval / consent fatigue:** a 1.3" round screen can't display a full command, diff, or path list (Copilot's `permission.requested` carries `fullCommandText`, `commands`, `possiblePaths`, `possibleUrls` [V local schema]) — users will tap "approve" on a truncated label. Attackers can craft commands where the harmless-looking prefix hides a payload (`npm test && curl … | sh`).
2. **Prompt injection → approval request → human taps yes:** the agent's requests may be triggered by malicious content in a repo/web page; the permission prompt is the *last line of defense*, and putting it on a glance-and-tap device weakens it. Copilot's `permissionRequest` hook with `behavior: allow` bypasses the normal flow [V], so a bug in the daemon (or a spoofed watch message) becomes **remote code execution on the PC** (`allow` for shell tools).
3. **Attack surface:** BLE/Wi-Fi/USB channel → anyone who pairs or replays can approve. A compromised or lookalike watch (or MITM without app-layer auth) approves arbitrary commands. Time-of-check/time-of-use: approval for request A must not apply to B; bind to `requestId` + hash of full command + `toolCallId` and expire in seconds.
4. **Hook semantics:** allow/deny decisions via hook are synchronous: the agent waits for the hook (timeout 30 s default [V]); a blocked daemon, disconnected watch, or timeouts must **fail closed** (fall through to normal prompt = the terminal prompt) rather than allow.
5. **Policy/compliance:** enterprise policy hooks and sandbox modes exist [V]; auto-approvals from a second device may violate org policy and audit trails.
6. **Physical risk:** accidental taps, watch worn near others, "approve all session" shortcuts (`canOfferSessionApproval` [V]) amplify blast radius.

Mitigations if offered at all: default **off**; only for low-risk classes (read-only tools, specific allowlisted commands already shown in `permissions-config.json`); never offer "approve for session"; show tool name + first N chars + a risk badge, require **long-press + second factor** (PIN on watch) for write/shell; "deny" and "focus to review" are always allowed; log everything on the PC; prefer the safer UX **"tap = jump to the terminal where I review and approve with the keyboard"**. This is exactly the project's focus feature, so v1 can omit remote approval entirely.

---------------------------------------------------------------------------

## 6. Recommended architecture

```
 agent CLI ──hook──▶ agentlink-hook (shim) ──unix sock/pipe──▶ agentlinkd ──▶ transport(s) ──▶ watch
 (Copilot/Claude/…)          ▲ env + ppid chain                   │  ▲ state aggregator,          (USB / BLE / Wi-Fi)
        └─ events.jsonl ─ file watcher (fallback/backfill) ───────┘  │ focus dispatcher
                                                                      └─ per-OS/per-terminal focus drivers
```

### Options and tradeoffs

**A. Hooks-first + per-terminal focus drivers (recommended).**
* Status: agent hooks → shim → daemon; file-tail and process scan as fallback/backfill.
* Focus: handle captured in hook; driver by `TERM_PROGRAM`/env; OS-level raise fallback.
* Pros: precise, event-driven (no polling), cheap, works with Copilot out of the box. Cons: requires installing hook config per agent; Aider/VS Code limited; many focus drivers to maintain.

**B. PTY-wrapper (`agentlink run -- <agent>`).**
* Pros: agent-agnostic (works with Aider/anything), sees OSC/bell/prompt text, exact tty. Cons: intrusive, breaks terminal features if sloppy, users forget to use it; still needs focus drivers.

**C. Passive observation only (files + process scan + terminal APIs).**
* Pros: zero agent config. Cons: coarse states, depends on undocumented formats (Copilot `events.jsonl`, Codex rollouts), polling/inotify overhead.

**D. VS Code/terminal-embedded extension model.** Extension reports status & focuses terminals (the only reliable way to focus a specific VS Code integrated terminal). Complements A for VS Code users.

### Transport options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| USB CDC (COBS+CBOR) | Zero pairing friction, reliable, charges, simple libs, debuggable | One host at a time, cable, DTR/RTS reset gotcha | **v1 baseline** |
| BLE GATT (NUS-like custom service, LESC) | Wireless, multi-central, low power, no network | Pairing UX per OS, MTU/chunking, OS permissions, library maturity (btleplug central-only OK) | **v2 primary wireless / multi-PC** |
| Wi-Fi mDNS + WebSocket/MQTT + Noise | Many hosts, high bandwidth, easy dev tooling, OTA | LAN-only, firewall/mDNS issues, higher watch power, need own app-layer auth | Optional v3; good for desks with ≥3 PCs |

Build the **protocol first and transport-agnostic** (envelope `{v, host, seq, type, payload}`; types `hello`, `pair`, `state`, `session_upsert`, `session_remove`, `focus`, `ack`, `ping`), implement USB first, add BLE with the same bytes (framing via COBS or length-prefix), keep Wi-Fi last.

### Agent → status-capture / focus matrix

| Agent | Status capture (primary → fallback) | macOS focus | Windows focus | Linux focus |
|---|---|---|---|---|
| **Copilot CLI** | `~/.copilot/hooks/*.json`: `sessionStart/End`, `userPromptSubmitted`, `preToolUse`, `agentStop`, `notification` (`permission_prompt`, `elicitation_dialog`, `agent_idle`), `errorOccurred` → fallback: tail `session-state/<id>/events.jsonl` (+ `inuse.<pid>.lock`, `open-sessions-state.json` `working`) | Terminal.app/iTerm2: AppleScript/py API by tty/session id; WezTerm/kitty: CLI + app activate; tmux: select-pane + client terminal; VS Code: extension URI; AX raise fallback | Windows Terminal: PID→HWND + UIA tab select + Alt-key/AttachThreadInput `SetForegroundWindow`; VS Code ext; tmux/WSL | X11: `$WINDOWID`/`wmctrl -ia`/xdotool; sway/Hyprland/KDE(kdotool)/GNOME (extension); tmux; else notify-only |
| **Claude Code** | `~/.claude/settings.json` hooks: `Notification`, `Stop`, `UserPromptSubmit`, `PreToolUse`, `PermissionRequest`, `SessionStart/End` → fallback `~/.claude/projects/**/*.jsonl` [M] | same as above | same | same |
| **Codex CLI** | `notify` program in `~/.codex/config.toml` (turn-complete) [M], hooks in newer versions [M verify] → fallback `~/.codex/sessions/**/rollout-*.jsonl` [M], OSC 9 via PTY wrapper | same | same | same |
| **Gemini CLI** | `~/.gemini/settings.json` hooks: `Notification (ToolPermission)`, `AfterAgent`, `BeforeAgent`, `SessionStart/End` [V] → fallback process scan | same | same | same |
| **Aider** | `--notifications-command` [M verify] → fallback PTY wrapper, process scan (`aider`), `.aider.chat.history.md` mtime | same | same | same |
| **VS Code Copilot agent mode** | VS Code agent hooks (same JSON schema as Copilot CLI VS-Code-compatible format [V]) + companion extension events | `vscode://agentlink…/focus` URI + `code -r <folder>`; AX raise | same URI; `code -r`; UIA/`SetForegroundWindow` on `Code.exe` | URI via `xdg-open`; X11 `wmctrl -a "<folder> - Visual Studio Code"`; Wayland compositor-specific |

(Focus method is determined by the *terminal*, not the agent; the agent column only changes the status source.)

### Suggested phasing
1. **Spike:** Python or Rust CLI: Copilot hook shim → daemon → JSON-lines over CDC to the watch; focus via tmux/WezTerm/Terminal.app on macOS only.
2. **v1:** Rust `agentlinkd` with tray, autostart, Copilot + Claude hooks, macOS/Windows/X11 focus drivers, USB transport, app-layer pairing (PIN), focus only.
3. **v2:** BLE multi-host, Gemini/Codex/Aider, Linux Wayland drivers, VS Code extension.
4. **v3 (optional):** Wi-Fi/MQTT, remote approvals (only with the mitigations in §5.3).

---------------------------------------------------------------------------

## 7. Gaps and things to verify before building
* Confirm via a dump-env test hook that Copilot `command`/`exec` hooks inherit `TERM_PROGRAM`, `WT_SESSION`, `ITERM_SESSION_ID`, `TMUX_PANE` and that `ppid` chain reaches the terminal (not fully verified).
* Verify whether Copilot CLI `remoteSteerable` / remote features expose a documented API (see https://github.com/github/copilot-cli).
* Codex `notify` payload and newer hook/approval events; Aider `--notifications-command`; Ghostty AppleScript support; Windows Terminal "focus tab by WT_SESSION" – all [M] items above.
* BLE multi-central limits on the actual StopWatch firmware (RAM, NimBLE config) and whether the board's Wi-Fi/BLE stack coexists with USB-Serial/JTAG under power-save.
* DTR/RTS reset behaviour of the ESP32-S3 USB-Serial/JTAG when opened by serialport-rs on all 3 OSes.
* The `https://docs.github.com/en/copilot/reference/hooks-reference` page was truncated during fetch (payload sections for `preToolUse`, `permissionRequest`, `agentStop` not read in full); the schema for those should be read directly before implementing the shim.

## 8. Sources
* Copilot hooks reference [V]: https://docs.github.com/en/copilot/reference/hooks-reference
* Copilot CLI config dir reference (linked from the above): https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-config-dir-reference
* github/copilot-cli repo: https://github.com/github/copilot-cli
* Claude Code hooks [V]: https://code.claude.com/docs/en/hooks
* Gemini CLI hooks reference [V]: https://github.com/google-gemini/gemini-cli/blob/main/docs/hooks/reference.md
* Codex advanced config (notify): https://developers.openai.com/codex/config-advanced
* Aider options: https://aider.chat/docs/config/options.html
* WezTerm CLI: https://wezterm.org/cli/cli/index.html ; kitty remote control: https://sw.kovidgoyal.net/kitty/remote-control/ ; iTerm2 scripting: https://iterm2.com/documentation-scripting.html
* Windows Terminal CLI args: https://learn.microsoft.com/en-us/windows/terminal/command-line-arguments ; SetForegroundWindow: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow
* kdotool: https://github.com/jinliu/kdotool
* ESP32-S3 USB-Serial/JTAG: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html
* serialport-rs: https://github.com/serialport/serialport-rs ; btleplug: https://github.com/deviceplug/btleplug ; bleak: https://github.com/hbldh/bleak ; go-serial: https://github.com/bugst/go-serial ; tinygo bluetooth: https://github.com/tinygo-org/bluetooth
* Meshtastic client API (framed protobuf over serial/BLE/TCP): https://meshtastic.org/docs/development/device/client-api/
* Local machine inspection (read-only, structure only): `~/.copilot/session-state/<id>/{events.jsonl,workspace.yaml,inuse.<pid>.lock}`, `~/.copilot/open-sessions-state.json`, `~/.copilot/settings.json`.
