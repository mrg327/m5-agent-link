# m5-agent-link: remote sessions over SSH

Status: built (issue `20261109-371b`). It was tested on the Mac with a stand-in for `ssh -R`, but
not yet against a real Windows machine (§6). Items marked **[unverified]** still need a live test.

## 1. Goal

Copilot CLI (or Claude Code) runs on a **remote Windows machine**. The watch and
`agentlinkd` stay on the **local Mac**. Remote sessions should appear on the watch next to
local ones, and the watch should be able to answer their questions and approvals.

Out of scope:
- A daemon or a watch link on the remote machine (DESIGN R10: one daemon per PC that owns a device).
- A cloud relay (DESIGN R11). The only path is an SSH connection between the user's own
  machines. Prompt text crosses that encrypted channel and nothing else.
- Remote approvals for agents that the local policy would not allow. Remote hooks get
  the same broker rules as local ones.

## 2. Why this needs work

The hook reaches the daemon through a local channel only:

| Side | Today |
|---|---|
| Mac daemon | listens on the Unix socket `~/.agentlink/hook.sock` (mode 0600) |
| Windows hook | connects to the named pipe `\\.\pipe\agentlink-<USERNAME>-<hash>` |

There are three problems:

1. OpenSSH cannot forward to or from a Windows named pipe. Win32-OpenSSH support for
   forwarding Unix sockets on the Windows end is also unreliable **[unverified]**. The
   remote end therefore has to be a **loopback TCP port** on Windows. OpenSSH can forward a
   remote TCP port to a *local Unix socket* (`ssh -R [bind:]port:local_socket`), so the Mac
   end can stay a socket.
2. A loopback TCP port on Windows has no owner check. Any process or user on that machine
   could connect to it. A forwarded connection also reaches the daemon as a local socket
   peer, so the daemon cannot tell it is remote.
3. A remote hook sends a `HostCtx` (pids, tty, `KITTY_WINDOW_ID`, `WT_SESSION`, ...) that
   describes the Windows machine. If the Mac's focus or scroll drivers used it, they would
   act on unrelated local processes.

## 3. Design

```
Windows (remote)                                       Mac (local)
copilot ─hook─► agentlink-hook.exe                     agentlinkd ──BLE──► watch
                 │ AGENTLINK_REMOTE=tcp:127.0.0.1:47533      ▲
                 ▼                                           │ ~/.agentlink/remote-<id>.sock
            127.0.0.1:47533 ◄═════ ssh -R (agentlink ssh) ════╛ (per window, token required)
```

### 3.1 Remote listeners (daemon)

- `hook.sock` stays local-only. A line on it can never claim to be remote, and no tunnel
  points at it. `{"ctl":...}` requests (status, attach) are accepted there only.
- Remote connections arrive on one of two kinds of socket (all mode 0600 in `~/.agentlink`,
  which `AGENTLINK_HOME` moves):
  - `remote-<id>.sock`: one per **attach**. `agentlink ssh` asks for it with
    `{"ctl":"remote_attach","ctx":{cwd, env, tty, pids}}` on `hook.sock`. The reply is
    `{"ok":true,"id":N,"socket":"<path>"}`. The attach and its socket live exactly as long
    as that ctl connection stays open; `agentlink ssh` holds it until ssh exits. Sessions
    that arrive through an attach can be focused and scrolled (§3.3).
  - `remote.sock`: always bound, for tunnels opened by hand (§4, RDP case). Its sessions are
    shown and answerable, but focus and scroll answer `unsupported`.
- Every remote connection must send `{"auth":"<token>","host":"<name>","os":"win"}` as its
  first line, within 10 s. There is no reply to it. The token is `~/.agentlink/remote.token`,
  64 hex chars written by `agentlink remote init`. It is re-read on every connection, so
  `agentlink remote init --rotate` applies at once; with no token file, every remote
  connection is refused. The compare is constant-time. A refusal is audited as
  `remote_auth_fail` (host, reason; never the token) and the connection is closed. A good
  line is audited as `remote_auth_ok`. After it, the normal hook protocol follows,
  including the blocking `await` flow; `ctl` lines are ignored. Remote hook lines are traced
  with source `remote`.
- The host name is reduced to `[A-Za-z0-9._-]`, at most 32 chars (empty: `remote`).

### 3.2 Remote transport in the hook (Windows)

- `AGENTLINK_REMOTE=tcp:<127.0.0.1|localhost|[::1]>:<port>`. When it is set, the hook uses
  only this transport, not the named pipe. It connects with a 300 ms timeout, sends the auth
  line and then the hook line. The token comes from `remote.token` in `AGENTLINK_HOME`, or
  `%USERPROFILE%\.agentlink`; the host name from `COMPUTERNAME` (else `HOSTNAME`, else
  `remote`).
- Any other address is refused with one stderr line, so a misconfiguration cannot send
  prompt text across the network unencrypted. A missing token sends nothing.
- A remote hook line also carries `"branch"`, read from the agent's cwd `.git/HEAD` on
  Windows. The daemon never reads remote paths on the Mac. The repo name is the last
  component of the cwd, split on `/` and `\`.
- All existing behaviour stays: fail-open, the 1.5 s deadline, exit 0, and the
  `AGENTLINK_WAIT_MS` wait for blocking Claude hooks. If the tunnel is down, the connect
  fails quickly and the agent shows its normal terminal prompt.

### 3.3 Hosts and routing

- The daemon gives each remote host name (case-insensitive) a host index, 1, 2, ..., stable
  while the daemon runs. Local sessions stay 0. Remote session keys are
  `r:<host>|<agent>:<session id>`, so they never merge with a local session.
- Sessions carry `host` and, for remote ones, `hn` (the host name, ≤ 16 chars; PROTOCOL.md
  §4). `hn` goes only to devices whose hello says proto ≥ 0.2, because older firmware rejects
  unknown keys. The watch shows the host name next to the repo of a remote session
  (UI-V2.md).
- The `HostCtx` of a remote session (Windows pids, env, tty) is kept for display only and is
  never passed to a focus or scroll driver. `registry.route(sid)` decides:
  - a local session: its own ctx;
  - a remote session whose latest line came through a **live** attach: that attach's local
    ctx, i.e. the kitty/tmux window running `agentlink ssh`;
  - otherwise `unsupported`, without calling a driver.
- So focus raises the local kitty or tmux window that holds the SSH session. Scroll goes to
  the same window: the kitty wheel kitten sends wheel or arrow input into the SSH pty, which
  reaches the remote Copilot TUI on its alternate screen. On the main screen, kitty scrolls
  its local history.
- If several remote sessions share one SSH window (for example panes inside it), focus and
  scroll reach the window, not the pane. This is accepted.

### 3.4 Lifetime and liveness

- When the tunnel drops, remote hooks fail open. Remote session records stay until their
  `SessionEnd`, as local ones do.
- When `agentlink ssh` exits, its attach socket is removed (audited as
  `remote_attach_close`) and that window's sessions stop being focusable.
- An open attention whose hook connection drops is cleared the way the broker already
  clears a hook that is gone.

## 4. Setup

Prerequisites:
- On Windows: OpenSSH Server running, and `AllowTcpForwarding` left at its default (`yes`).
  `GatewayPorts` left at `no`, so the forward binds loopback only.
- Copilot CLI hooks on Windows point at `agentlink-hook.exe`, as for a local Windows install
  (`agentlink hooks print`). Build it on Windows with
  `cargo build --release -p agentlink-hook`.
- On the Mac: `agentlinkd` running, from this version on.

Once:
```sh
# Mac
agentlink remote init                  # ~/.agentlink/remote.token (0600); --rotate replaces it
ssh <win-host> mkdir .agentlink
scp ~/.agentlink/remote.token <win-host>:.agentlink/remote.token
ssh <win-host> icacls .agentlink\remote.token /inheritance:r /grant:r "%USERNAME%:R"
```
```powershell
# Windows, user environment (new shells pick it up)
setx AGENTLINK_REMOTE tcp:127.0.0.1:47533
```

Each time:
```sh
# Mac, in kitty (or tmux): opens the shell and the tunnel together
agentlink ssh <win-host> [ssh args]    # = ssh -o ExitOnForwardFailure=yes -R 127.0.0.1:47533:~/.agentlink/remote-<id>.sock <win-host>
# then start copilot in that shell
```

`--port` changes 47533; `AGENTLINK_SSH` changes the ssh binary. If the port is already
taken on Windows (a second Mac, or a stale sshd forward), `ExitOnForwardFailure` makes ssh
exit instead of opening a shell without the tunnel. `agentlink status` lists the remote
hosts and the attached windows.

**RDP-only case.** If you reach Windows over RDP and there is no SSH session from the Mac,
the Windows side can open the tunnel instead:
`ssh -N -L 127.0.0.1:47533:/Users/<me>/.agentlink/remote.sock <me>@<mac>`. This needs
Remote Login enabled on the Mac **[unverified with Win32-OpenSSH]**. Focus and scroll are
unsupported in this case (§3.3).

## 5. Security notes

- The remote channel can show text on the watch and receive answers. It cannot run
  commands on the Mac: a remote `HostCtx` never reaches a focus or scroll driver.
- The token gates the Windows loopback port. Without it, another user on the Windows
  machine could put fake requests on the watch and read the answers. Revoke a token with
  `agentlink remote init --rotate`; old copies then fail auth.
- Everything is audited as for local sessions. Decisions also record the session's `host`
  index, and auth successes and failures are audited too.
- Approving from the watch has the same limits as locally (`hold_ms >= 1000`,
  first answer wins, stale answers acked as `stale`).

## 6. Verification

- Unit tests: token and constant-time compare, host sanitize, auth lines, remote keys and
  host index, routing (no attach, live attach, dropped attach), `hn` only for proto ≥ 0.2,
  the hook's loopback check and TCP transport, and the `agentlink ssh` argv.
- On the Mac, with a fake `ssh` that forwards TCP to the attach socket: remote sessions
  `@WIN-TEST` appear with Windows-path repo names, a blocking hook falls back after its
  wait, a bad token and a non-loopback address are refused, and the attach socket goes away
  when ssh exits.
- **Still to do on real hardware:** Mac + Windows 11 with OpenSSH Server. Covers the
  tunnel, approve/deny from the watch, tunnel drop (fail-open), a port already in use, and
  focus and scroll through kitty into the Windows Copilot TUI.
