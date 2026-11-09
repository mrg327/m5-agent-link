# m5-agent-link: remote sessions over SSH

Status: built (issue `20261109-371b`). The tunnel, auth and session path have been tested
against a real Windows 11 machine. A full interactive Copilot session is still pending (issue
`20261109-e9b2`, §6). Items marked **[unverified]** still need a live test.

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

- When the tunnel drops, remote hooks fail open.
- When `agentlink ssh` exits, its attach socket is removed (audited as
  `remote_attach_close`) and the sessions that arrived through it are removed too (`gone` to
  the watch): their `SessionEnd` can no longer arrive. A session that is still running remote
  side reappears with its next hook through a new `agentlink ssh`. Sessions on the shared
  `remote.sock` (no attach) stay until their `SessionEnd`, as local ones do.
- An open attention whose hook connection drops is cleared the way the broker already
  clears a hook that is gone.

## 4. Setup

Prerequisites:
- On Windows: OpenSSH Server running, and `AllowTcpForwarding` left at its default (`yes`).
  `GatewayPorts` left at `no`, so the forward binds loopback only.
- Copilot CLI hooks on Windows point at `agentlink-hook.exe`, as for a local Windows install
  (`agentlink hooks print`). Use an absolute path in `exec`
  (`C:\Users\<me>\.agentlink\bin\agentlink-hook.exe`) so it doesn't depend on PATH. Build it on
  Windows with `cargo build --release -p agentlink-hook`, or cross-compile on the Mac with
  `cargo install cargo-xwin` then
  `cargo xwin build --release -p agentlink-hook --target x86_64-pc-windows-msvc`
  (needs `rustup target add x86_64-pc-windows-msvc`), and `scp` the exe over.
- On the Mac: `agentlinkd` running, from this version on.

Windows quirks seen on Windows 11 with OpenSSH 9.5:
- **Admin accounts** ignore `~\.ssh\authorized_keys`: sshd reads
  `C:\ProgramData\ssh\administrators_authorized_keys`, which must be readable only by
  Administrators and SYSTEM
  (`icacls <file> /inheritance:r /grant Administrators:F /grant SYSTEM:F`).
- **The default ssh shell may be PowerShell**, not cmd, so `$` in a remote command is expanded
  twice. Send scripts with `powershell -NoProfile -EncodedCommand <UTF-16LE base64>`.
- **Copilot sign-in in a key-auth ssh session:** Windows Credential Manager isn't reachable
  from a public-key logon, so Copilot reports "Saved sign-in could not be read from credential
  storage". Fix it by authenticating in that session (`/login`, or a token in the env vars the
  Copilot CLI documents). This is Windows behaviour, not agent link.
- `setx AGENTLINK_REMOTE ...` is picked up by new ssh sessions.

### 4.1 Saved remotes (recommended)

Profiles live in `~/.agentlink/remotes.json` (0600) on the Mac. A name is 1-32 of
`[A-Za-z0-9_-]`.

```sh
# Once per machine: save it and onboard it (Windows, or Linux/WSL with --os unix)
agentlink remote add <name> <user@host> [--os windows|unix] [--dir <path>] [--cmd <command>] \
    [--port 47533] [--ssh-arg <arg>]... [--setup [--hook-exe <exe>] [--key <pub>]] [--force]
agentlink remote list | edit <name> [--dest ..] [--dir ..] [--cmd ..] | rm <name> | setup <name>

# Each time
agentlink ssh <name>              # tunnel + cd <dir> + run <cmd>; ssh closes when <cmd> exits
agentlink ssh <name> --no-cmd     # just a shell with the tunnel
```

`edit` keeps fields you don't pass; `--dir ""` or `--cmd ""` clears one. Extra ssh args after the
name (`agentlink ssh <name> -- -v`) also skip the on-connect command. Anything that isn't a
saved name is passed to ssh as a destination, as before.

`remote setup` (also `add --setup`) onboards a machine from the Mac, and is safe to re-run
(for example after `remote init --rotate` or a new hook build). The first run asks for the
password once, so run it in a terminal:
1. checks key login with `BatchMode`; if that fails it installs `--key`
   (default `~/.ssh/id_ed25519.pub`) with one password prompt (on Windows admin accounts,
   into `administrators_authorized_keys`);
2. creates `~/.agentlink/bin` and `~/.copilot/hooks`, and makes an existing token writable again;
3. `scp`s the hook and the token, creating the token if it's missing;
4. makes the token readable only by the user, persists `AGENTLINK_REMOTE`, and writes
   `~/.copilot/hooks/agentlink.json` with the absolute hook path.

| | Windows | Linux / WSL (`--os unix`) |
|---|---|---|
| Default hook | `~/.agentlink/dist/windows-x64/agentlink-hook.exe` | `~/.agentlink/dist/linux-<x64\|arm64>/agentlink-hook`, picked by the remote's `uname -m` |
| Build it | `cargo xwin build --release -p agentlink-hook --target x86_64-pc-windows-msvc` | `cargo zigbuild --release -p agentlink-hook --target x86_64-unknown-linux-musl` (static; needs `brew install zig`, `cargo install cargo-zigbuild`, the rustup target) |
| `AGENTLINK_REMOTE` | `setx` (new shells) | a line tagged `# agentlink-remote` in `~/.profile`, `~/.bashrc`, and `~/.zshrc` if present, replaced on re-run |

For a WSL sshd on another port, save it with `--ssh-arg=-p --ssh-arg=2222` (scp gets `-P`).

On Windows the on-connect command runs as
`powershell -NoLogo -EncodedCommand <Set-Location <dir>; <cmd>; exit $LASTEXITCODE>`, so `<cmd>`
is PowerShell syntax. If the account's default ssh shell is PowerShell, the outer shell turns
the command's exit code into 0 or 1; the session still closes when the command exits.
On Unix it runs as `export AGENTLINK_REMOTE=…; cd -- <dir> && exec "$SHELL" -lic '<export>; <cmd>'`:
an interactive login shell, so tools that rc files put on PATH (nvm) are found. The export is
repeated inside because rc files run after it. A leading `~/` in `<dir>` means the remote home.
Both forms set `AGENTLINK_REMOTE` to the profile's port, so `--port` works without re-running setup.
On Linux the hook reports the kernel hostname, because bash doesn't export `HOSTNAME`.

### 4.2 Manual setup

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
- **Real Windows 11 (build 26200, Win32-OpenSSH 9.5), cross-compiled `agentlink-hook.exe`:**
  `agentlink ssh` opens the `-R` forward to the Mac's Unix socket. The hook reaches it over
  TCP with token auth (`remote_auth_ok`, `os: win`), and the session appears as `@<HOST>` and
  is attached to the kitty window. Hooks returned in 29-457 ms, and the attach socket was
  removed when ssh exited.
- **Still to do on real hardware:** a real Copilot session over the tunnel (blocked on
  Copilot sign-in in a key-auth ssh session, see §4), approve/deny from the watch, tunnel
  drop (fail-open), a port already in use, and focus and scroll through kitty into the
  Windows Copilot TUI.
