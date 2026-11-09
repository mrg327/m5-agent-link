# m5-agent-link: remote sessions over SSH (design, not built)

Status: design only. Tracking issue: `20261109-371b`.
Nothing below is implemented yet. Items marked **[unverified]** still need a live test.

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
Windows (remote)                                      Mac (local)
copilot ─hook─► agentlink-hook.exe                    agentlinkd ──BLE──► watch
                 │ AGENTLINK_REMOTE=tcp:127.0.0.1:47533     ▲
                 ▼                                          │ ~/.agentlink/remote.sock
            127.0.0.1:47533 ◄════ ssh tunnel ════► (separate listener, token required)
```

### 3.1 A separate remote listener (daemon)

- `agentlinkd` binds a second Unix socket, `~/.agentlink/remote.sock` (mode 0600). It only
  does this when `~/.agentlink/remote.token` exists. `agentlink remote init` creates that
  file: 32 random bytes, hex, mode 0600.
- Every connection on `remote.sock` is treated as remote. Its first line must be
  `{"auth":"<token>","host":"<name>","os":"win"}`. The token is compared in constant time;
  on a mismatch the daemon closes the connection and writes an audit entry. After that the
  usual hook protocol follows unchanged.
- `hook.sock` stays local-only. A message on `hook.sock` can never claim to be remote, and
  the tunnel never points at `hook.sock`.

### 3.2 Remote transport in the hook (Windows)

- New environment variable `AGENTLINK_REMOTE=tcp:127.0.0.1:<port>`. When it is set, the
  hook connects there instead of the named pipe and sends the auth line first. The token
  comes from `%USERPROFILE%\.agentlink\remote.token` (or `AGENTLINK_HOME`), and `host` from
  `COMPUTERNAME`.
- The hook only accepts loopback addresses. `tcp:` to any other address is refused, so a
  misconfiguration cannot send prompt text across the network unencrypted.
- All existing behaviour stays: fail-open, the 1.5 s deadline, exit 0, and the
  `AGENTLINK_WAIT_MS` wait for blocking Claude hooks. If the tunnel is down, the connect
  fails quickly and the agent shows its normal terminal prompt.
- Secure the token file with the user's ACL only (`icacls`, set by `agentlink remote init`
  when run on Windows, or by the setup steps below).

### 3.3 Hosts and routing

- The daemon gives each remote `host` name a host index (1, 2, ...). Local sessions stay 0.
  The protocol already carries `session.host` (PROTOCOL.md: "always 0 in v1"). The welcome
  or snapshot then needs a host table (`{idx, name, os}`) so the watch can show a host badge.
  That is a protocol and firmware follow-up. Until it exists, the watch shows remote
  sessions without a badge.
- The `HostCtx` of a remote session is stored for display only, never passed to local
  drivers. Session ids are minted by the daemon, so remote and local sids cannot collide.
- **Focus and scroll** for a remote session use the *local* terminal that holds the SSH
  connection, not the remote context:
  - `agentlink ssh <host> [ssh args]` is a thin wrapper. It picks a free port, runs
    `ssh -R 127.0.0.1:<port>:$HOME/.agentlink/remote.sock <host> ...`, and registers
    `{host, local HostCtx of this terminal}` with the daemon. The local context is the
    wrapper's own `KITTY_WINDOW_ID`, `TMUX_PANE` and pid chain.
  - Focus on a remote session raises that local kitty/tmux window. Scroll goes to the same
    window: the kitty wheel kitten sends the wheel/arrow input into the SSH pty, so it
    reaches the remote Copilot TUI on its alternate screen. On the main screen, kitty
    scrolls its local history.
  - If no local window is registered for the host (tunnel started by hand, RDP, VS Code
    Remote, ...), focus and scroll answer `unsupported` and the watch hides them for that
    session.
- If several remote Copilot sessions share one SSH window (for example several panes
  inside the remote session), focus and scroll reach the window, not the pane. This is
  accepted.

### 3.4 Lifetime and liveness

- When the tunnel drops, remote hooks fail open. The remote sessions' records age out like
  local ones, through the normal `SessionEnd`/staleness path.
- `agentlink ssh` unregisters its window when ssh exits.
- An open attention whose hook connection drops gets the `Clear` path the broker already
  uses for a hook that is gone (d-answer-gone).

## 4. Setup (target user flow)

Prerequisites:
- On Windows: OpenSSH Server running, and `AllowTcpForwarding` left at its default (`yes`).
  `GatewayPorts` left at `no`, so the forward binds loopback only.
- Copilot CLI hooks on Windows point at `agentlink-hook.exe`, as for a local Windows install
  (`agentlink hooks print`).
- The same `agentlink-hook.exe` version as the Mac daemon, or one with a compatible
  protocol major.

Once:
```sh
# Mac
agentlink remote init                  # creates ~/.agentlink/remote.token (0600)
scp ~/.agentlink/remote.token <win-host>:.agentlink/remote.token
ssh <win-host> icacls %USERPROFILE%\.agentlink\remote.token /inheritance:r /grant:r "%USERNAME%:R"
```
```powershell
# Windows, user environment (persist with setx)
setx AGENTLINK_REMOTE tcp:127.0.0.1:47533
```

Each time:
```sh
# Mac, in kitty (or tmux): opens the shell and the tunnel together
agentlink ssh <win-host>               # = ssh -R 127.0.0.1:47533:$HOME/.agentlink/remote.sock <win-host>
# then start copilot in that shell
```

A fixed port (47533 above) keeps `AGENTLINK_REMOTE` static on Windows. If the port is
already taken on Windows (a second Mac, or a stale sshd forward), ssh warns "remote port
forwarding failed". The wrapper then exits with that message instead of opening a shell
without the tunnel.

**RDP-only case.** If you reach Windows over RDP and there is no SSH session from the Mac,
the Windows side can open the tunnel instead:
`ssh -N -L 127.0.0.1:47533:/Users/<me>/.agentlink/remote.sock <me>@<mac>`. This needs
Remote Login enabled on the Mac **[unverified with Win32-OpenSSH]**. Focus and scroll are
unsupported in this case (§3.3).

## 5. Security notes

- The remote channel can show text on the watch and receive answers. It cannot run
  commands on the Mac: remote `HostCtx` never reaches a focus or scroll driver.
- The token gates the Windows loopback port. Without it, another user on the Windows
  machine could put fake requests on the watch and read the answers. Revoke a token with
  `agentlink remote init --rotate`, which re-creates the file; old tunnels then fail auth.
- Everything is audited as it is for local sessions, plus the `host` name and auth failures.
- Approving from the watch has the same limits as locally (`hold_ms >= 1000`,
  first answer wins, stale answers acked as `stale`).

## 6. Work items

1. daemon: `remote.sock` listener, token auth, host index table, remote ctx kept out of
   drivers, `agentlink remote init [--rotate]`, `agentlink ssh` (+ window registration over
   the CLI's status IPC).
2. hook: `AGENTLINK_REMOTE=tcp:` transport (loopback only) with the auth line; unit test for
   the loopback check and the auth line.
3. focus/scroll: route `host != 0` to the registered local window, else `Unsupported`.
4. protocol + firmware: host table in welcome/snapshot, host badge on session rows.
5. docs: README environment table (`AGENTLINK_REMOTE`), PROTOCOL `host` wording.
6. live test: Mac + Windows 11 with OpenSSH Server. Covers the tunnel, approve/deny from
   the watch, tunnel drop (fail-open), a wrong token, and port in use. Also check focus and
   scroll through kitty into the Windows Copilot TUI.
