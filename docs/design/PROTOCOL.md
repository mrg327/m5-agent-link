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

  `Device Information Service` and `Battery Service` are standard 0x180A/0x180F.
  - **Fragmentation:** each write/notification is `[FIRST|LAST|seq:6] data…` (`FIRST=0x80`, `LAST=0x40`). `seq` starts at 0 on the FIRST fragment and wraps mod 64. A `seq` gap, a continuation without a FIRST, or a message over `max_msg` drops the partial message. A FIRST in the middle of a message drops the partial and starts a new one.
  - **Fragment size:** both sides send fragments of MTU−3 bytes (ATT header 3; the 1-byte fragment header leaves MTU−4 data bytes). The daemon reads the negotiated MTU from its BLE stack per frame and caps writes at 244 B; until the MTU exchange is reported (MTU 23) it writes 20 B, and after a failed large write it falls back to 20 B for the rest of the link. `AGENTLINK_BLE_WRITE_LEN` pins the daemon's write size. Receivers reassemble by header only, so any fragment size up to MTU−3 is valid. The device also requests LE Data Length 251 so that one MTU-sized fragment fits one link-layer packet.
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
  proto: [uint, uint],             ; [major, minor] = [0, 3]
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
  ? replies: [0*6 tstr]       ; quick replies, each ≤ 24 chars (proto 0.4)
}
```
- `replies` is sent only to a device whose `hello` says proto ≥ 0.4 (an older decoder rejects an unknown key). Its presence means the daemon can type into terminals: `cmd type` and `cmd cancel` (§5). The list is the user's own `~/.agentlink/replies.txt` (one per line, `#` comments, ≤ 6 × 24 chars), and empty without one: there are no built-in defaults.

## 4. State messages (daemon → device)

### Session object
```cddl
session = {
  sid: uint,                 ; short id assigned by daemon (stable for the session lifetime)
  ag: "copilot"/"claude"/"cloud"/tstr,
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
  ? ok: bool                 ; result of step n; absent while it runs          ┘
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
- `snapshot { sessions: [* session], away?: [* event] }`: a full replacement. It is sent on connect and whenever the daemon thinks the device has drifted. The `away` list holds the events the device missed while disconnected; the device shows them as "while you were away".
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
  ? opts: [* tstr],          ; the agent's own choices (≤ 4, each ≤ 24 chars): a question's options, or a
                             ; permission's "Yes" / "Yes, don't ask again" / "No"
  acts: [* "approve"/"deny"/"focus"/"reply"/"ack"],
  exp?: uint,                ; unix time after which device must drop it
  grant_mask?: uint          ; bit i set = opts[i] grants permission (approve-class: the device commits
                             ; it only by a hold); sent whenever opts is (0 for a plain question)
}
clear = { rid: uint, why: "answered_pc"/"answered_dev"/"expired"/"cancelled" }
```
The device must show approve/deny **only if** they are in `acts`. The daemon includes them only for prompts the agent itself raised (DESIGN Q6). An option is answered with `cmd { op: "reply", rid, arg: index }`; the daemon maps the index back to the agent's choice (UI-V2 §7).

## 5. Commands (device → daemon)

```cddl
cmd = {
  op: "focus"/"ack"/"snooze"/"approve"/"deny"/"reply"/"cancel"/"detail"/"notify_done"/"quiet"/"scroll"/"type"/"dictate"/"kb_pick"/"kb_del"/"kb_send"/"kb_end",
  ? sid: uint,
  ? rid: uint,               ; required for approve/deny/reply
  ? arg: uint / tstr / scroll_arg,  ; snooze minutes (the watch sends 30), reply option index, quiet duration,
                             ; or the scroll argument (the CDDL names the union cmd_arg)
  hold_ms?: uint             ; for approve: measured hold-to-confirm duration (daemon enforces ≥ 1000)
}
scroll_arg = { unit: "lines"/"pages"/"to_end", delta: int, ? seq: uint }   ; delta < 0 = up (older); to_end: sign = top / bottom
result = { ok: bool, ? err: "stale"/"denied"/"unsupported"/"no_window"/"timeout", ? msg: tstr, ? pos: scroll_pos }
scroll_pos = { lines_above: uint, at_bottom: bool, ? total: uint, ? preview: tstr }   ; only in answers to scroll
```
- `focus`: the daemon runs the FocusDriver for that session. On `no_window`, the device shows "Open terminal not found".
- `scroll` (UI-V2 §6): the daemon moves the session's terminal scrollback (tmux copy mode first, else kitty remote control; other terminals answer `unsupported` and the device hides the item for that session). The daemon sums a burst into one move while the previous one runs, answers at most one `result` per 250 ms and only when the position changed or the move failed, and clamps `delta` to ±500 lines / ±20 pages. `pos.preview` is the terminal's top visible line, ≤ 48 chars, sent only when the device's privacy is `preview` or `full`; it is never written to the audit log or the trace. Scroll is read-only on the PC and is not audited.
- `type` (proto 0.4, issue b150): `arg` is an index into `welcome.replies`, or, from 100 up, into the session's own answers `session.rp` (proto 0.5: `arg` 101 = `rp[1]`); the daemon types that reply and Enter into the session's terminal (kitty only today, else `unsupported`). The device never sends text. Refused with `denied` while the session has an open request, while it is `input` with `sub` approval or question, or while a numbered selection list is on screen: Enter there would pick the dialog's highlighted option. Audited with the index, not the text.
- `cancel` (proto 0.4): interrupts the session's running turn by sending Esc to its terminal. It needs a hold like `approve` (`hold_ms` ≥ 1000, else `denied`). Audited.
- `ack`: marks the session seen (all devices). On an `error` session it also dismisses the error: the
  session becomes `idle`.
- `snooze`: `arg` minutes (the watch sends 30); `0` cancels. The daemon stores `snz` and sends it to
  every device. A snoozed session's attention and finish events do not buzz or glow and its ring segment
  is dimmed; cards still appear. The snooze lapses by the clock; the daemon does not send a clear.
- `notify_done`: `arg` 1 (or absent) arms, 0 disarms. The daemon sets `ntf`; when the session next
  enters `done` or `error` the same delta carries `ntf: false`, and each device that saw `ntf` true
  buzzes once.
- `detail`: the daemon replies with `detail { rid|sid, text }`, which holds the full text. This is sent only when privacy ≠ `meta`.
- `approve`/`deny`/`reply` on a `rid` that has already been cleared returns `err: "stale"`, so a race against the terminal answer is safe. The daemon's approval broker holds the agent's blocking hook for that `rid` (DESIGN §6a); the first of {device answer, terminal answer, timeout} settles it, and only a device answer ever becomes a decision for the agent.

## 6. Audio: voice dictation (proto 0.6, issue 0ea9)
```
audio_start { aid, codec: "adpcm16k", sid? }   device → daemon
audio       { aid, seq, data: bstr }             device → daemon (≤ 240 B each)
audio_end   { aid }                              device → daemon
transcript  { aid, text, autosend_ms: 0 }        daemon → device
cmd { op: "dictate", sid, arg: aid, hold_ms }    device → daemon
```
- The daemon offers voice with `welcome.voice: true` (only to devices with proto ≥ 0.6) when whisper.cpp is installed (`AGENTLINK_WHISPER_CLI` / `AGENTLINK_WHISPER_MODEL`, else `~/tools/whisper.cpp` with `ggml-base.en.bin`). A device with a mic advertises `feature_voice`.
- `adpcm16k`: 16 kHz mono IMA ADPCM in self-contained blocks: 4 header bytes (first sample, int16 LE; step index; 0), then two samples per byte, low nibble first; 240 bytes hold 473 samples (~30 ms). A lost block only loses its 30 ms. The device caps a recording at 15 s; the daemon drops one past 20 s.
- On `audio_end` the daemon decodes the blocks in `seq` order, runs whisper (the WAV exists only while it runs, 0600 in `~/.agentlink/voice/`), drops whisper's own annotations (`[BLANK_AUDIO]`, `(music)`), and answers `transcript` with `text` ≤ 240 chars, `""` when nothing was heard. `autosend_ms` is always 0: nothing is sent without the user.
- `dictate` types the transcript of `aid` and Enter into the session's terminal, after a hold (`hold_ms` ≥ 1000, else `denied`), with the same refusals as `type` (an open request, a question or approval waiting, a selection list on screen). A transcript is used once, only for the session it was recorded for, and forgotten after 5 min (`stale`).
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
- `kb_start.keys`: per key 10 bytes LE: u16 code point, i16 x, i16 y, u16 w, u16 h (top-left rectangle, display px). Code points: a letter types itself, `0x20` space, `0x08` backspace (deletes the last word), `0x0E` layer switch (handled on the device, which resends `kb_start` with the same `kid`). A repeated `kb_start` replaces the layout and keeps the composition.
- `kb_stroke.pts`: per point 6 bytes LE: i16 x, i16 y, u16 ms since touch-down, sampled at the touch rate (~30 Hz). A stroke is one word. A stroke whose extent stays under 18 px is a tap on the key under its first point.
- On each stroke the daemon decodes (SHARK2-style shape and location channels plus a word-frequency and context prior), answers `kb_cands` with up to four words, best first, and types the first one (with a leading space after a previous word) into the session's terminal; `typed` is the index it typed. `kb_pick` replaces the last typed word with candidate `arg` of the latest `kb_cands`; `kb_del` erases the last word. `kb_send` presses Enter after a hold (`hold_ms` ≥ 1000, else `denied`) and starts a new composition. `kb_end` closes the keyboard and, with `arg` 1, erases what it typed and did not send.
- Typing has the same refusals as `type` (an open request, a question or approval waiting, a selection list on screen): then `kb_cands` still carries the candidates but `err: "denied"` and no `typed`. Kitty only today, else `unsupported`.
- Strokes, candidates and typed words never reach the audit log; the trace replaces `keys`, `pts` and `cands` with `<private>`. The daemon logs only word counts.

## 7. Misc
- `batt { pct, chg: bool }` (device → daemon, on change ≥ 5% or on charge state change) is shown in the tray.
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
