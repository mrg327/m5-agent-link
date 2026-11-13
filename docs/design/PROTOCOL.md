# agentlink protocol v0 (draft)

> Status: **[D] v0 decisions: integer CBOR keys, always-on app-layer AEAD, zcbor C codegen.** The protocol is transport-agnostic and device-agnostic. See DESIGN.md §3.1 and §5.

## 1. Layers

```
┌ app messages (CBOR maps, schema below) ┐
├ security envelope (after pairing)      ┤  AEAD (ChaCha20-Poly1305), per-link session key, counters
├ fragmentation (BLE only)               ┤  1-byte header: [FIRST|LAST|seq:6]
└ transport                              ┘  BLE GATT · USB CDC (COBS + CRC16) · WebSocket (binary frame)
```

- **Encoding:** CBOR (RFC 8949). **[D]** The map keys are **small integers** to save bytes (~25%). The names in this document are the *symbolic* names. The integer assignment is normative in `protocol/agentlink.cddl` (e.g. `t=0, id=1, re=2, b=3`), and the daemon's `agentlink-cli decode` prints symbolic names for debugging.
  - Device side: `tinycbor` / `zcbor`.
  - Daemon side: `ciborium` / `minicbor`.
  - The schema is normative in CDDL (`protocol/agentlink.cddl`), with test vectors in `protocol/vectors/`.
- **Max message size:** each device advertises `caps.max_msg` (StopWatch: 4096). The daemon truncates text fields to fit.
- **BLE service:**

  | Item | UUID / value |
  |---|---|
  | Service | `a9e1f000-7c3b-4d52-9e18-b06a35c4d2f1` |
  | `rx` (write w/o response, daemon→device) | `a9e1f001-7c3b-4d52-9e18-b06a35c4d2f1` |
  | `tx` (notify, device→daemon) | `a9e1f002-7c3b-4d52-9e18-b06a35c4d2f1` |
  | Advertised name | `AgentLink-XXXX` |
  | MTU | 247 requested |
  | PHY | 2M preferred |

  `Device Information Service` and `Battery Service` are standard 0x180A/0x180F. Battery Level (0x2A19) is read and notify: the device notifies a subscribed central when the percent changes by 1 or more (the device's own linear 3.3-4.2 V figure, sampled at least once a minute; [POWER.md](POWER.md)).
  - **Fragmentation:** each write/notification is `[FIRST|LAST|seq:6] data…` (`FIRST=0x80`, `LAST=0x40`). `seq` starts at 0 on the FIRST fragment and wraps mod 64. A `seq` gap, a continuation without a FIRST, or a message over `max_msg` drops the partial message. A FIRST in the middle of a message drops the partial and starts a new one.
  - **Fragment size:** both sides send fragments of MTU−3 bytes (ATT header 3; the 1-byte fragment header leaves MTU−4 data bytes). The daemon reads the negotiated MTU from its BLE stack per frame and caps writes at 244 B; until the MTU exchange is reported (MTU 23) it writes 20 B, and after a failed large write it falls back to 20 B for the rest of the link. `AGENTLINK_BLE_WRITE_LEN` pins the daemon's write size. Receivers reassemble by header only, so any fragment size up to MTU−3 is valid. The device also requests LE Data Length 251 so that one MTU-sized fragment fits one link-layer packet. A central without data length extension (the BCM4350C0 in some Linux laptops) refuses that; the device then answers the MTU exchange with 239 instead of 247, so that each ATT packet (243 B with L2CAP) fills nine 27-byte link-layer packets exactly instead of ten with an 8-byte tail.
  - **Discovery:** the daemon connects to devices that advertise the service or a name starting with `AgentLink-`. `AGENTLINK_BLE_NAME` pins one device by name.
  - **Security (current state):** no bonding and no encryption yet (`sm_bonding = 0`, no-IO capability). The LESC passkey and app-layer AEAD in §8 are planned, not implemented.
- **USB CDC:** each frame is `0x00 ‖ COBS(payload ‖ crc16_be) ‖ 0x00`. The CRC is CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`, no reflection, xorout 0), and the payload is at most 4096 B.
  - **Shared stream:** the same port carries ESP-IDF log text and `#`-prefixed JSON debug lines. These never contain `0x00`. A receiver splits on `0x00`: chunks that COBS-decode and pass the CRC are frames, and everything else is text.
  - **Liveness:** after `hello` the daemon sends `ping` every 5 s. The device re-handshakes (sends a new `hello`) after 12 s without a frame.
  - **Ping ids:** the CDDL declares `id` as an unbounded `uint`, but the device decodes it as `uint32`. The daemon's USB pings therefore use ids from 2^30 up, so they fit in 32 bits.
  - **Reset hazard:** the daemon opens the port **without touching DTR/RTS**. Explicitly setting DTR=false and then RTS=false resets the ESP32-S3.
  - **Discovery:** USB VID `0x303A`, PID `0x1001` (ESP32-S3 USB Serial/JTAG). `AGENTLINK_USB_PORT` overrides the port path.

## 2. Envelope

Every message is a CBOR map:

| key | type | meaning |
|---|---|---|
| `t` | tstr | message type (below) |
| `id` | uint | sender's message counter (monotonic per link; used for acks and replay protection) |
| `re` | uint? | the message `id` this message replies to |
| `b` | map | body |

Unknown `t` → ignore. Unknown keys in `b` → ignore. A breaking change means a bump of `proto` major.

## 3. Handshake & lifecycle

```
device                                 daemon
  │── hello {proto, dev, caps, fw} ───►│
  │◄── welcome {proto, host, time, cfg}│   (version negotiated: min(major) must match)
  │◄── snapshot {sessions[]} ──────────│
  │◄── session / attention / clear ────│   (incremental)
  │── cmd {...} ──────────────────────►│
  │◄── result {ok|err} ────────────────│
  │◄─► ping/pong (only if idle > 60 s; BLE supervision timeout covers most)
```

- **Re-handshake:** a `hello` on a live link is a full re-handshake (device reboot or link timeout). The link's message ids reset, and the daemon replies with `welcome` and a full `snapshot`. The daemon's device registry is keyed by the device `uid`, so a re-handshake updates the same entry.
- **USB timing:** on USB the daemon pings every 5 s and the device re-handshakes after 12 s of silence (§1).
- **Transport arbitration** (device side, `link.c`): the device sends `hello` on every transport that is up, and the first `welcome` claims the link; frames from the other transports are then ignored. **USB is preferred when plugged in** (2026-11-10): while welcomed on BLE or WS with a USB host attached, the device also offers a `hello` on USB, at once and then every 5 s (every 30 s while those sends time out, i.e. a host that is attached but not reading). A `welcome` on USB takes the link over: it is a new link (cards dropped, a snapshot follows) and the old transport is ignored from then on. When the active transport drops, the device falls back to a handshake on whatever is still up. The daemon that loses the device simply stops hearing from it.

### `hello` (device → daemon)
```cddl
hello = {
  proto: [uint, uint],             ; [major, minor] = [0, 20]
  dev: { class: tstr,              ; "watch" | "desk" | "pad" | "sim" | ...
         board: tstr,              ; "m5stopwatch"
         fw: tstr,                 ; semver
         uid: bstr },              ; stable device id (e.g. eFuse MAC hash)
  caps: {
    disp?: { shape: "round"/"rect", w: uint, h: uint, color: bool },
    input: [* ("touch"/"btn"/"rotary"/"imu")],
    nbtn?: uint,
    haptic: bool, spk: bool, mic: bool, batt: bool,
    max_msg: uint,
    feats: [* tstr]                ; "approve","reply","voice","badge","focus"
  },
  ? batt: uint                     ; percent
}
```

### `welcome` (daemon → device)
```cddl
welcome = {
  proto: [uint, uint],
  host: { id: bstr, name: tstr, os: "mac"/"win"/"linux" },
  time: { unix: uint, tz_off: int },   ; device RTC sync (RX8130CE)
  cfg: { privacy: "meta"/"preview"/"full",
         alert: "full"/"quiet"/"silent",
         quiet_until?: uint },
  ? replies: [0*6 tstr],      ; quick replies, each ≤ 24 chars (proto 0.4)
  ? pointer: bool             ; key 7 (proto 0.20): the daemon drives its machine's pointer (§6g)
}
```
- `replies` is sent only to a device whose `hello` says proto ≥ 0.4 (an older decoder rejects an unknown key). Its presence means the daemon can type into terminals: `cmd type` and `cmd cancel` (§5). The list is the user's own `~/.agentlink/replies.txt` (one per line, `#` comments, ≤ 6 × 24 chars), and empty without one: there are no built-in defaults.

## 4. State messages (daemon → device)

### Session object
```cddl
session = {
  sid: uint,                 ; short id assigned by daemon (stable for the session lifetime)
  ag: "copilot"/"claude"/"cloud"/"shell"/tstr,   ; "shell" = a job, a terminal command (proto 0.9, JOBS.md)
  st: "start"/"work"/"input"/"error"/"done"/"idle"/"ended",
  ? sub: "approval"/"question",      ; when st = input
  repo?: tstr,               ; ≤ 32 chars, ellipsized by daemon
  br?: tstr,                 ; ≤ 32 chars
  ? tool: tstr,              ; current tool, e.g. "bash", "edit"
  t0: uint,                  ; start unix time (device computes elapsed)
  tst: uint,                 ; time of last state change
  ? sum: tstr,               ; preview ≤ 80 chars (privacy-dependent)
  seen: bool,
  host: uint,                ; host index: 0 = this PC; 1.. = a remote host (REMOTE.md)
  ? hn: tstr,                ; remote host name (≤ 16 chars), with host != 0 only; sent only to devices
                             ; whose hello proto is ≥ 0.2 (older firmware rejects unknown keys)
  ? snz: uint,               ; snoozed until (unix), 0 = not snoozed          ┐ sent only to devices
  ? ntf: bool,               ; "notify when done" armed                        │ whose hello proto
  ? n: uint,                 ; tool steps in the current turn                  │ is ≥ 0.3
  ? ok: bool,                ; result of step n; absent while it runs          ┘
  ? eta: uint,               ; a job's expected total run time in s; 0 = unknown  ┐ proto ≥ 0.9
  ? exit: uint,              ; a job's exit status, with the delta into done/error ┘
  ? ctx: uint,               ; context window used, percent; 255 = unknown         ┐
  ? mdl: tstr,               ; model short name (≤ 16 chars)                         │ proto ≥ 0.10
  ? pm: perm_mode,           ; default / plan / accept_edits / auto / dont_ask / bypass │ (GAUGES.md)
  ? pr: uint,                ; open pull request number; 0 = none (not under meta)  │
  ? prs: pr_state,           ; none / pending / approved / changes / draft          ┘
  ? hush: bool,              ; only with the delta that enters done / error; proto ≥ 0.12 (PRESENCE.md)
  ? fc: uint,                ; files the turn changed (≥ 1)                          ┐
  ? la: uint,                ; lines added, with lr                                  │ proto ≥ 0.14
  ? lr: uint,                ; lines removed                                          │ (DONE-SUMMARY.md)
  ? tp: bool,                ; the last recognised test run: true passed, false failed │ key 30-34
  ? dur: uint,               ; the turn's length in seconds                           ┘
  ? qd: tstr,                ; the queued message's start, <= 48 chars; "" = none (proto 0.17, key 35, §6d)
  ? lm: uint,                ; the agent's last message: its id, 0 = none        ┐ proto ≥ 0.18, keys 36-38
  ? ll: uint,                ; its length in bytes as cleaned (≤ 4096)           │ (§6e, LAST-MESSAGE.md)
  ? lp: tstr                 ; its start, ≤ 160 bytes; "" under privacy meta     ┘
}
```
- `q` / `rp` (keys 17, 18, proto 0.5): when an agent's turn ends on a question, the daemon reads it from the turn's last message (Claude Code's Stop hook carries `last_assistant_message`) and sends the question (≤ 120 chars) with up to 3 answers (≤ 24 chars): the options of an "A or B?" question, else `Yes, go ahead` / `No` for a yes/no one, else none. The watch lists them first under Reply, with the question above. They are cleared (`""`, `[]`: a delta cannot remove a key) as soon as the agent works again. Agent content: never sent under privacy `meta`, and only to devices with proto ≥ 0.5.
- `sum` is the current step: `"<tool> · <argument>"` (the shell command's first line, a file's last two
  path components, a URL or a search pattern), ≤ 80 chars. The daemon sends it only when the device's
  privacy is not `meta`; otherwise the device sees `tool` alone.
- `seen` is the daemon's: it becomes `false` when the session enters `done` or `error` and `true` on
  `ack`, and every device gets the change.
- `n` counts `pretooluse` events since the last prompt; `ok` arrives with the matching `posttooluse`
  (false for a failed, denied or rejected tool). The device builds its step timeline (UI-V2 §4.5) from
  these two keys and `sum`.
- **Jobs (proto 0.9, [JOBS.md](JOBS.md)).** A command the user started in a terminal and that has run for
  ten seconds is a session with `ag: "shell"`: `work` while it runs, `done` for exit 0, `error` otherwise.
  `t0` is the command's start and `tst` its end; `tool` is the program, `sum` the command line. `eta` is the
  median of its earlier runs. The daemon sends shell sessions only to devices with proto ≥ 0.9.
- **Gauges (proto 0.10, [GAUGES.md](GAUGES.md)).** `ctx`, `mdl` and `pr` / `prs` come from Claude Code's status
  line (`agentlink-hook statusline`), `pm` from the hooks' `permission_mode`. A status line only updates a session
  the hooks created. `ctx: 255` clears the figure (after `/compact`); `pr: 0` says no pull request is open. The
  pull request is withheld under privacy `meta`.
- `snapshot { sessions: [* session], away?: [* event] }`: a full replacement. It is sent on connect and whenever the daemon thinks the device has drifted (its link fell behind the daemon's update stream). A snapshot replaces sessions only: the device keeps the cards of sessions it still lists. So after a drift the daemon follows it with a `clear` `cancelled` for each request it had sent on this link that ended meanwhile (the real reason is lost), then re-sends every open request; a re-sent `rid` updates its card in place (issue 20261109-2ec5). The `away` list holds the events the device missed while disconnected; the device shows them as "while you were away".
- `session { session }`: an upsert. The daemon sends only the changed keys plus `sid` (a delta).
- `gone { sid }`: the session has been removed.

### Attention
```cddl
attention = {
  rid: uint,                 ; request id; required for approve/deny
  sid: uint,
  kind: "approval"/"question"/"error"/"done",
  ? title: tstr,             ; e.g. "Run shell command"
  ? text: tstr,              ; preview (≤ 160) — full text only via `detail` request
  ? opts: [* tstr],          ; the agent's own choices (≤ 4, each ≤ 24 chars): a question's options, or the
                             ; "Yes" / "No" of a prompt read off a terminal (Copilot). Since 0.13 a Claude Code
                             ; permission has none: "always allow" is the rule below
  acts: [* "approve"/"deny"/"focus"/"reply"/"ack"/"dictate"],
  exp?: uint,                ; unix time after which device must drop it
  grant_mask?: uint,         ; bit i set = opts[i] grants permission (approve-class: the device commits
                             ; it only by a hold); sent whenever opts is (0 for a plain question)
  ? left: uint,              ; ms left in the answer window when sent       ┐ proto ≥ 0.11
  ? win: uint,               ; the whole answer window in ms                 │ (ANSWER-WINDOW.md)
  ? len: uint,               ; the full text's length in bytes, when text is cut ┘
  ? hush: bool,              ; no buzz, glow or reminders for this request; proto ≥ 0.12 (PRESENCE.md)
  ? rule: tstr,              ; the standing rule, exactly as Claude Code saves it   ┐ proto ≥ 0.13,
  ? scope: uint,             ; 0 this session, 1 this project (local settings)      │ always together
  ? wide: bool,              ; the rule ends in a wildcard                           ┘ (STANDING-RULES.md)
  ? qs: [1*4 ask_q]          ; AskUserQuestion's questions; proto ≥ 0.18, key 16 (§6e, LAST-MESSAGE.md)
}
ask_q = { h: tstr, q: tstr, o: [1*4 { l: tstr, ? d: tstr }], ? m: bool }   ; header, question, options, multi
clear = { rid: uint, why: "answered_pc"/"answered_dev"/"expired"/"cancelled"/"handed_back" }
```
- **Answer window (proto 0.11, [ANSWER-WINDOW.md](ANSWER-WINDOW.md)).** `left` and `win` let the device count
  down on its own clock (a re-sent request after a reconnect carries what is left now); `expired` means the
  window ran out and the terminal asks now. `len` says the preview is not the whole text (cut, or reshaped) and
  how long the full text is; past 4096 bytes the device cannot fetch it. `handed_back` answers the device's own
  Open on PC (`focus` with the `rid`). Keys 9-11 and `handed_back` go only to devices with proto ≥ 0.11 (older
  ones get `cancelled`); `len` is sent under privacy `meta` too, the text is not.

- **Presence (proto 0.12, [PRESENCE.md](PRESENCE.md)).** `hush: true` on an `attention`, or on the session delta
  that enters `done` / `error`, says the user is at the keyboard with that session's terminal in front of them:
  the device holds the arrival buzz, the glow and the reminders (attention) or the finished buzz (session) and
  changes nothing else. It is only ever `true`, only sent to devices with proto ≥ 0.12, and never stored (a
  delta without it says nothing). The card, its actions and the answer window are as ever; the window itself is
  shorter when the user is at the keyboard (`left` / `win` say so). An old device buzzes as before.

- **Standing rules (proto 0.13, [STANDING-RULES.md](STANDING-RULES.md)).** `rule` is one of the request's own
  `permission_suggestions`, as Claude Code will save it (`Bash(npm test:*)`, several rules one per line, ≤ 160
  bytes of printable ASCII and line breaks); `scope` says where (`0` session, `1` the user's own
  `.claude/settings.local.json`; no wider scope is ever sent); `wide` that a rule ends in a wildcard. The device
  offers "Always allow" only for a rule it can show whole, and grants it with `approve` carrying `rh`. Keys 13-15
  go only to devices with proto ≥ 0.13, under every privacy setting (the rule is no more than the preview).
  `scope` is a uint, not the CDDL enum, so a value a device does not know drops the rule, not the request.
- **Done summary (proto 0.14, [DONE-SUMMARY.md](DONE-SUMMARY.md)).** `fc`, `la` / `lr`, `tp` and `dur` (keys 30-34)
  say what the turn that just ended did, tallied by the daemon from Claude Code's hooks. Each is optional and an
  absent one is unknown, never zero. They come with the delta that enters done / error (and in a snapshot); a
  delta that changes `st` clears the device's copy and one carrying any of them replaces it whole. Counts and
  seconds only: no file name is sent, and privacy `meta` does not withhold them. Only to devices with proto ≥ 0.14.

The device must show approve/deny **only if** they are in `acts`. The daemon includes them only for prompts the agent itself raised (DESIGN Q6). An option is answered with `cmd { op: "reply", rid, arg: index }`; the daemon maps the index back to the agent's choice (UI-V2 §7).

## 5. Commands (device → daemon)

```cddl
cmd = {
  op: "focus"/"ack"/"snooze"/"approve"/"deny"/"reply"/"cancel"/"detail"/"notify_done"/"quiet"/"scroll"/"type"/"dictate"/"kb_pick"/"kb_del"/"kb_send"/"kb_end"/"rerun"/"unqueue"/"answer"/"pointer",
  ? sid: uint,
  ? rid: uint,               ; required for approve/deny/reply
  ? arg: uint / tstr / scroll_arg,  ; snooze minutes (the watch sends 30), reply option index, quiet duration,
                             ; or the scroll argument (the CDDL names the union cmd_arg)
  hold_ms?: uint,            ; for approve: measured hold-to-confirm duration (daemon enforces ≥ 1000)
  ? rh: uint,                ; with approve (proto 0.13): also save the request's standing rule. FNV-1a 32 of
                             ; the rule, a 0 byte, scope and wide (0/1) as the device showed them; hold ≥ 1500
  ? q: bool,                 ; with type / dictate (proto 0.17, key 6): queue it if the session can't take input (§6d)
  ? picks: [1*4 uint],       ; with answer (proto 0.18, key 7): per question, the options picked (bit mask)  ┐ §6e
  ? aids: [1*4 uint],        ; with answer (key 8): per question, the recording of a free answer, 0 = none  ┘
  ? ptr: pointer_arg         ; with pointer (proto 0.20, key 9): move / click / scroll (§6g)
}
scroll_arg = { unit: "lines"/"pages"/"to_end", delta: int, ? seq: uint }   ; delta < 0 = up (older); to_end: sign = top / bottom
result = { ok: bool, ? err: "stale"/"denied"/"unsupported"/"no_window"/"timeout", ? msg: tstr, ? pos: scroll_pos }
scroll_pos = { lines_above: uint, at_bottom: bool, ? total: uint, ? preview: tstr }   ; only in answers to scroll
```
- `focus`: the daemon runs the FocusDriver for that session. On `no_window`, the device shows "Open terminal not found".
  With a `rid` (proto 0.11) it also hands that open request back to the terminal at once: the hook returns no
  decision, the terminal prompt appears, and devices get `clear` `handed_back`. It answers nothing.
- `scroll` (UI-V2 §6): the daemon moves the session's terminal scrollback (tmux copy mode first, else kitty remote control; other terminals answer `unsupported` and the device hides the item for that session). The daemon sums a burst into one move while the previous one runs, answers at most one `result` per 250 ms and only when the position changed or the move failed, and clamps `delta` to ±500 lines / ±20 pages. `pos.preview` is the terminal's top visible line, ≤ 48 chars, sent only when the device's privacy is `preview` or `full`; it is never written to the audit log or the trace. Scroll is read-only on the PC and is not audited.
- `type` (proto 0.4, issue b150): `arg` is an index into `welcome.replies`, or, from 100 up, into the session's own answers `session.rp` (proto 0.5: `arg` 101 = `rp[1]`); the daemon types that reply and Enter into the session's terminal (kitty only today, else `unsupported`). The device never sends text. Refused with `denied` while the session has an open request, while it is `input` with `sub` approval or question, or while a numbered selection list is on screen: Enter there would pick the dialog's highlighted option. Audited with the index, not the text.
- `cancel` (proto 0.4): interrupts the session's running turn by sending Esc to its terminal. It needs a hold like `approve` (`hold_ms` ≥ 1000, else `denied`). Audited.
- `cancel` on a job (`ag: "shell"`) sends SIGINT to the foreground process group of the shell's tty instead of
  Esc; `stale` when nothing runs there.
- `rerun` (proto 0.9): types a finished job's command line and Enter into its terminal, after a hold
  (`hold_ms` ≥ 1000, else `denied`). `denied` while that shell runs anything, `stale` when the session is not a
  finished job, `unsupported` without a typing driver. Audited without the command text.
- `ack` on a job removes its card (`gone`).
- `ack`: marks the session seen (all devices). On an `error` session it also dismisses the error: the
  session becomes `idle`.
- `snooze`: `arg` minutes (the watch sends 30); `0` cancels. The daemon stores `snz` and sends it to
  every device. A snoozed session's attention and finish events do not buzz or glow and its ring segment
  is dimmed; cards still appear. The snooze lapses by the clock; the daemon does not send a clear.
- `notify_done`: `arg` 1 (or absent) arms, 0 disarms. The daemon sets `ntf`; when the session next
  enters `done` or `error` the same delta carries `ntf: false`, and each device that saw `ntf` true
  buzzes once.
- `detail` (proto 0.11, [ANSWER-WINDOW.md](ANSWER-WINDOW.md) §4): `{sid, rid, arg: offset}` asks for open request
  `rid`'s full text from byte `offset`. The daemon replies with `detail { rid, sid, text, off, total }` chunks of
  at most 1024 bytes, cut on UTF-8 boundaries, in order, then a `result`: `stale` for a request that is not open,
  `denied` under privacy `meta`, past 4096 bytes or for an offset inside a character, `unsupported` from a device
  below 0.11. The text keeps line breaks and spells out control and undrawable characters (`^[`, `\u{a0}`). The
  device keeps a chunk only for the rid it asked for, at the offset it has reached, with the announced total.
  Without a `rid` (proto 0.18, §6e) it asks for session `sid`'s last message.
- `approve` with `rh` (proto 0.13, [STANDING-RULES.md](STANDING-RULES.md)): approve, and have the hook pass the
  request's standing rule back to Claude Code as `updatedPermissions`. `denied` when the request offers no rule,
  when `rh` is not the hash of the rule it offers (another request's, another scope, a broader rule), or when
  `hold_ms` < 1500; the request stays open. Audited as `standing_rule` (rule, scope, destination, session key,
  time). An older daemon ignores `rh` and approves once.
- `approve`/`deny`/`reply` on a `rid` that has already been cleared returns `err: "stale"`, so a race against the terminal answer is safe. The daemon's approval broker holds the agent's blocking hook for that `rid` (DESIGN §6a) for the answer window ([ANSWER-WINDOW.md](ANSWER-WINDOW.md)); the first of {device answer, terminal answer, Open on PC, window over, hook gone} settles it, and only a device answer ever becomes a decision for the agent.

## 6. Audio: voice dictation (proto 0.6, issue 0ea9)
```
audio_start { aid, codec: "adpcm16k", sid? }   device → daemon
audio       { aid, seq, data: bstr }             device → daemon (≤ 240 B each)
audio_end   { aid }                              device → daemon
transcript  { aid, text, autosend_ms: 0 }        daemon → device
cmd { op: "dictate", sid, rid?, arg: aid, hold_ms }  device → daemon
```
- The daemon offers voice with `welcome.voice: true` (only to devices with proto ≥ 0.6) when whisper.cpp is installed (`AGENTLINK_WHISPER_CLI` / `AGENTLINK_WHISPER_MODEL`, else `~/tools/whisper.cpp` with `ggml-base.en.bin`). A device with a mic advertises `feature_voice`.
- `adpcm16k`: 16 kHz mono IMA ADPCM in self-contained blocks: 4 header bytes (first sample, int16 LE; step index; 0), then two samples per byte, low nibble first; 240 bytes hold 473 samples (~30 ms). A lost block only loses its 30 ms. The device caps a recording at 60 s (15 s before proto 0.16); the daemon drops one past 70 s. The audio is streamed as it is recorded, so the length costs the device no memory.
- On `audio_end` the daemon decodes the blocks in `seq` order, runs whisper (the WAV exists only while it runs, 0600 in `~/.agentlink/voice/`), drops whisper's own annotations (`[BLANK_AUDIO]`, `(music)`), and answers `transcript` with `text` of at most what the device holds: 2047 UTF-8 bytes for proto ≥ 0.16, else 255 (a longer text is cut on a character boundary and ends with `…`), `""` when nothing was heard. The daemon keeps exactly the text it sent, so `dictate` types what the user read. The device pages a long transcript and allows the hold only once its last page has been on screen. `autosend_ms` is always 0: nothing is sent without the user.
- `dictate` types the transcript of `aid` and Enter into the session's terminal, after a hold (`hold_ms` ≥ 1000, else `denied`), with the same refusals as `type` (an open request, a question or approval waiting, a selection list on screen). A transcript is used once, only for the session it was recorded for, and forgotten after 5 min (`stale`).
- **Dictating an answer (proto 0.8, issue 7941).** An `attention` whose `acts` include `"dictate"` takes a dictated answer, for example a free-text field in a Copilot form. The device offers "Dictate..." on that card, and sends `dictate` with the card's `rid`. The transcript then answers that card instead of being typed as a new prompt, so the refusals for an open request do not apply. An unknown or settled `rid` is `stale`; a card without `"dictate"` is `denied`. The daemon puts `"dictate"` in `acts` only for devices with proto ≥ 0.8 and voice.
- Speech and transcripts never reach the trace or the audit log (the trace replaces `data` and `text` with `<private>`); the daemon logs only the duration and the word count.

## 6a. Keyboard: KEYS swipe input (proto 0.7, issue 1e57)
```
kb_start  { kid, sid, keys: bstr }                    device → daemon (≤ 64 keys × 10 B)
kb_stroke { kid, seq, pts: bstr }                     device → daemon (≤ 200 points × 6 B)
kb_cands  { kid, seq, cands: [≤ 4 tstr], typed?, err? } daemon → device
cmd { op: "kb_pick", sid, arg: index }                device → daemon
cmd { op: "kb_del",  sid }                            device → daemon
cmd { op: "kb_send", sid, hold_ms }                   device → daemon
cmd { op: "kb_end",  sid, arg: 1 erase / 0 keep }     device → daemon
```
- The daemon offers KEYS with `welcome.keys: true` only to devices that advertise `feature_keys` with proto ≥ 0.7. The device draws the keyboard and owns its geometry; the daemon owns the lexicon and the decoder. The device still never sends text: only key rectangles, touch points and candidate indices.
- `kb_start.keys`: per key 10 bytes LE: u16 code point, i16 x, i16 y, u16 w, u16 h (top-left rectangle, display px). Code points: a letter types itself, `0x20` space, `0x08` backspace (deletes the last word), `0x0E` layer switch (handled on the device, which resends `kb_start` with the same `kid`). Any other printable ASCII code point (the symbols layer: digits, `-_./:~=+@#%&*?!'`) is typed literally and glued to the word before it and the word after it, so `check_dram` `.` `py` gives `check_dram.py`; a later `kb_pick` replaces only the part after the symbol. A repeated `kb_start` replaces the layout and keeps the composition.
- `kb_stroke.pts`: per point 6 bytes LE: i16 x, i16 y, u16 ms since touch-down, sampled at the touch rate (~30 Hz). A stroke is one word. A stroke whose extent stays under 18 px is a tap on the key under its first point.
- On each stroke the daemon decodes (SHARK2-style shape and location channels plus a word-frequency and context prior), answers `kb_cands` with up to four words, best first, and types the first one (with a leading space after a previous word) into the session's terminal; `typed` is the index it typed. `kb_pick` replaces the last typed word with candidate `arg` of the latest `kb_cands`; `kb_del` erases the last word. `kb_send` presses Enter after a hold (`hold_ms` ≥ 1000, else `denied`) and starts a new composition. `kb_end` closes the keyboard and, with `arg` 1, erases what it typed and did not send.
- Typing has the same refusals as `type` (an open request, a question or approval waiting, a selection list on screen): then `kb_cands` still carries the candidates but `err: "denied"` and no `typed`. Kitty only today, else `unsupported`.
- Strokes, candidates and typed words never reach the audit log; the trace replaces `keys`, `pts` and `cands` with `<private>`. The daemon logs only word counts.

## 6b. Usage (proto 0.10, [GAUGES.md](GAUGES.md))
```
usage { at, h5?, h5r?, h5o?, wk?, wkr?, wko? }      daemon → device (type 23)
```
- The plan's rate-limit windows from Claude Code's status line (`rate_limits.five_hour` / `seven_day`): used
  percent (rounded down), reset time, and the time it runs out at the current pace when that comes first. `at` is
  when the daemon last read them. A full replacement: an absent window is unknown, never zero.
- Sent after the snapshot to a device with proto ≥ 0.10 (unless the last reading is over an hour old), on every
  change of the figures (a projection must move 5 minutes), at least every 5 minutes while readings arrive, and
  after a lag. Only status lines from this machine feed it.
- The device shows a window while the clock is set, the window has not reset and `at` is under an hour old.

## 6c. Firmware update (proto 0.15, [FIRMWARE.md](FIRMWARE.md) §9, issue b769)
```
fw_begin { size, sha: bstr(32), ver?, patch? }   daemon → device (type 24); size 0 stops
fw_patch { psize, base: bstr(32), bsize }        proto 0.21: the chunks carry a patch, not the image
fw_chunk { off, data: bstr(≤ 2048) }             daemon → device (type 25)
fw_ack   { st, next, msg? }                      device → daemon (type 26)
st: go 0 | done 1 | busy 2 | bad 3 | fail 4 | stopped 5
```
- Only to a device with proto ≥ 0.15, and only when asked (`agentlink ota <image.bin>`); never on its own.
- `fw_begin` opens a transfer of a whole app image (`sha` is its SHA-256). The device answers `fw_ack go`
  with `next` = 0, or with the offset it already holds when a transfer of the same `sha` and `size` is open
  (resume after a dropped link). It answers `busy` (recording, low battery and not charging, another link
  updating it) or `bad` (does not fit, or this board cannot update itself) with a short `msg`.
- The daemon then sends `fw_chunk`s in order from `next`, at most 4 unanswered. The device writes each chunk
  whose `off` equals its `next` and acks `go` with the new `next`; a chunk at any other offset is not written,
  and its ack carries the expected `next`, from which the daemon resends.
- After the last byte the device verifies the image (an app for this chip, the SHA-256, and a signature by
  the key the running firmware was signed with) and acks `done`, then restarts about a second later into
  the new image; or acks `bad` (refused) / `fail` (flash error) and keeps running the old one.
- `fw_begin` with `size` 0 stops the transfer; the device acks `stopped`. The device also sends `stopped`
  on its own when the user cancels (B held) or no chunk has arrived for 30 s.
- The new image runs unconfirmed until a daemon welcomes it; it then keeps itself. If it is not welcomed
  within 5 minutes, or resets before that, the previous image boots again (rollback). The daemon compares the
  `fw` in the next `hello` with the image's version to tell the two apart.
- **Delta update (proto 0.21, issue ef2c).** To a device with proto ≥ 0.21 the daemon may send a patch
  instead of the image: `fw_begin.patch` gives the patch length (`psize`) and the base it applies to, the first
  `bsize` bytes of the device's running app slot, by SHA-256 (`base`). `size` and `sha` still describe the
  rebuilt image. The patch is a detools sequential patch (bsdiff, heatshrink window 8 lookahead 7, the format of
  Espressif's esp_delta_ota). The device hashes its base before it answers; on a mismatch it acks `bad`
  "Base mismatch" and the daemon starts again with the whole image. Otherwise `fw_chunk.off` and `fw_ack.next`
  count patch bytes; the device rebuilds the image into the spare slot as the patch arrives and checks it
  exactly as a whole image (`sha`, signature, rollback). A transfer resumes as before (same `sha`, `size`
  and patch). The daemon picks the base from images it has sent or built (by the `fw` in `hello`) and sends
  the whole image when it has none or the patch is not smaller.
- Chunks never reach the trace (it shows the size only).

## 6d. Queued messages (proto 0.17)
A quick reply or a dictation can be sent to a session whose agent is working; it is typed at once, mid-turn
(the agent takes it in as it goes). Only while typing would answer something else is it held, and typed later.
Until 2026-11-13 the daemon also held it through `start`/`work` until the turn ended; that made replies stale.
- `type` or `dictate` with `q: true` and no `rid`: when the session can take input now (no open request, not
  `input` with `sub` approval or question; `start` and `work` can), the daemon types it at once, as without `q`.
  Otherwise it keeps the text (a dictation's transcript is taken at once) and answers `ok`. A session holds one
  queued message; a new one replaces it. `q` with a `rid` is `denied`.
- The daemon sets `session.qd` to the start of the text (≤ 48 chars, cut with `…`) and sends it to every device
  with proto ≥ 0.17; the device shows it and offers to cancel it. `qd` is agent content: never written to the
  audit log or the trace. Under privacy `meta` it is sent as `"…"` (something is queued, no text).
- When the session can take input again (the approval or question is answered and no request is open), the
  daemon waits about 2 s for the terminal to settle, then types the text and Enter with the same checks as `type` (a numbered selection on screen refuses). On success it clears `qd`
  (`""`); on a refusal or a typing failure it keeps the message and tries again at the session's next change.
- `unqueue` (`sid`): drops the session's queued message and clears `qd`; `stale` when none is queued.
- The message is dropped with its session (`gone`). It lives in the daemon's memory only and is lost when the
  daemon restarts. It never expires otherwise. Audited when typed, with the source (reply index or recording),
  not the text.

## 6e. Last message and questions (proto 0.18, [LAST-MESSAGE.md](LAST-MESSAGE.md))
- **Last message.** When a Claude Code turn ends (`Stop`, `last_assistant_message`), the daemon keeps the final
  message, markdown cleaned to plain text and cut at 4096 bytes, one per session, in memory, and sends `lm` (a new
  id), `ll` (its length) and `lp` (its start, ≤ 160 bytes) with the delta that enters done; a new prompt clears them
  (`0`, `0`, `""`). `cmd detail {sid, arg: off}` **without a rid** asks for it: `detail {sid, text, off, total, lm}`
  chunks of ≤ 1024 bytes, then `result ok`; `stale` with no message, `denied` under privacy `meta`. The device keeps
  a chunk only for the session and `lm` it asked for. Agent content: `lp` and the chunks' text never reach the
  trace or the audit log; under `meta`, `lp` is `""` (`lm` and `ll` still say a message exists). Keys 36-38 and
  `detail.lm` (key 5) only to devices with proto ≥ 0.18.
- **Questions.** Claude Code's `PermissionRequest` for `AskUserQuestion` is a held request (the answer window) of
  kind `question` with `qs`: every question's header (≤ 16 bytes), text (≤ 160), options (label ≤ 48, description
  ≤ 120) and `m` for multi-select. `cmd answer {sid, rid, picks, aids, hold_ms}` (op 19) answers them all at once,
  one entry per question: a bit mask of options, or the recording (`aid`) of a dictated free answer. `denied`
  without a 1000 ms hold, for a wrong count, no or several picks on a single-choice question, a bit past the
  options or both a pick and a recording; `stale` for a request that is not open or a recording without a
  transcript; nothing is used up by a refusal. The hook answers `allow` with `updatedInput` = the tool's input plus
  `answers` (`{question: label}`, several labels joined with `", "`, or the transcript). `PreToolUse` no longer
  holds a question. A device below 0.18 gets no `qs`: one single-choice question keeps `opts` (`reply`, or
  `dictate` with the `rid`), anything else is Open on PC only.

## 6f. Power report (proto 0.19, [POWER.md](POWER.md))
```
power { mv, mvf, vin, chg, usb, pct, on, dim, off, deep, lk, ble, itvl, seq, nd }   device → daemon (type 27)
```
- The device sends it only to a daemon whose `welcome` says proto ≥ 0.19: right after the welcome, then every 5
  minutes while welcomed (a send that fails is tried again 30 s later). Nothing is sent, and nothing extra wakes
  the device, while no daemon listens: the figures add up until the next report goes out.
- `mv` is the last raw battery reading and `mvf` the filtered one the device's own percent `pct` comes from (linear
  3.3-4.2 V); `vin` the input voltage, `chg` charging, `usb` external power present. All in mV. Voltage only: the
  device cannot measure current.
- `on`, `dim`, `off` and `deep` are the ms since the previous report with the display on at full look, on and
  dimmed to the ambient face, off (standby), and in deep sleep. Their sum is the time the report covers. Deep sleep
  is measured by the RTC across the wake reboot; the not-yet-sent figures survive it in RTC memory (`nd` deep sleeps
  ended in this span). A power-on or reset loses them: `seq` (the report number since power-on) starts at 0 again.
- `lk` is the ms welcomed by a daemon (any transport), `ble` the ms with a BLE central connected, and `itvl` the BLE
  connection interval in force now in µs (0 = no BLE connection).
- The daemon appends each report to `~/.agentlink/power-reports.csv` and keeps the last one per device for
  `agentlink status`; `agentlink power` reads the file. An older daemon ignores the message type.

## 6g. Trackpad (proto 0.20, [EXPERIMENTS.md](EXPERIMENTS.md))
```
cmd { op: "pointer", ptr: { dx?: int, dy?: int, btn?: "left"/"right", n?: uint, wheel?: int } }   device → daemon
```
- The daemon moves the pointer of **its own machine** (not the host a session runs on): by (`dx`, `dy`) pixels
  (positive = right / down), then presses and releases `btn` once, then scrolls `wheel` notches (positive = down,
  towards newer content). It clamps one command to ±2000 px, click count 3 and ±20 notches.
- `n` is the click count of that press: 1 = a click, 2 = the second click of a double click (default 1). The watch
  sends every tap at once and sets 2 on a tap that follows the last one closely. macOS gets the count as the
  event's click state; Windows and X11 count double clicks themselves from the timing.
- `welcome.pointer` (key 7) is sent to a device with proto ≥ 0.20 when the daemon has a backend: macOS
  (CoreGraphics events), Windows (`SendInput`), Linux on X11 with `xdotool` (not under Wayland).
- No `result` when it works. A failure is answered at most once per second: `unsupported` (no backend) or `denied`
  (the OS refused; on macOS the message asks to allow agentlinkd under Privacy & Security > Accessibility).
- The trace leaves out commands that only move; clicks and scrolls are traced. Nothing is audited.

## 7. Misc
- `batt { pct, chg: bool }` (device → daemon, on change ≥ 5% or on charge state change) is shown in the tray. The current firmware does not send it; the power report (§6f) carries more.
- `cfg { … }` (daemon → device) is a partial update of the `welcome.cfg` fields.
- `ping {}` / `pong {}`.
- `log { lvl, msg }` (device → daemon) is debug only and is enabled from the daemon.

## 8. Security
1. **Pairing** establishes a long-term shared secret per (device, host).
   - BLE: LE Secure Connections with a passkey shown on the device. An app-level key confirmation (`pair` message) then binds the secret.
   - USB-first: X25519 key exchange over CDC, with a confirmation code shown on both screens.
2. **Per connection:** `hello`/`welcome` carry nonces. The session key is derived as `HKDF(secret, nonces)`. After that, every message is AEAD-sealed. `id` is the nonce counter, so a replay is rejected.
3. A daemon accepts `approve`/`deny`/`reply`/`cancel` only from paired devices, and only for a live `rid`. It logs every action.
4. A simulator (WS on localhost) pairs like USB-first. It is never exposed beyond 127.0.0.1.

## 9. Sizing sanity (StopWatch over BLE)
A typical `session` delta is 30–80 B, and a `snapshot` with 10 sessions is about 1.2 KB, which is 5–6 BLE fragments. When idle there is no traffic, apart from the BLE link-layer empty packets at the connection interval (~1 s). This fits the multi-day battery target.

## 10. Open
- **[D]** Integer keys; symbolic names are kept in CDDL and debug tooling.
- **[D]** App-layer AEAD is always on, on every transport.
- **[D]** `zcbor` generates the C encoders/decoders from the CDDL. The Rust side is hand-written (`minicbor` derive with `#[n(k)]` indices). Both sides must pass the shared test vectors in `protocol/vectors/` in CI.
- Enum values (`st`, `op`, `kind`, …) are also encoded as small uints in CDDL.
