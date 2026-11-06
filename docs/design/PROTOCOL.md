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
  - **Fragment size:** the device sends fragments of MTU−4 data bytes (ATT header 3 + fragment header 1). The daemon cannot read the negotiated MTU on every OS, so it writes 20-byte fragments (header included) by default. `AGENTLINK_BLE_WRITE_LEN` overrides this.
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
- TODO: transport arbitration (behaviour when the same device is reachable over both USB and BLE).

### `hello` (device → daemon)
```cddl
hello = {
  proto: [uint, uint],             ; [major, minor] = [0, 1]
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
         quiet_until?: uint }
}
```

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
  host: uint                 ; host index (multi-PC ready; always 0 in v1)
}
```
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
  ? opts: [* tstr],          ; for questions with choices (≤ 4, each ≤ 24 chars)
  acts: [* "approve"/"deny"/"focus"/"reply"/"ack"],
  exp?: uint                 ; unix time after which device must drop it
}
clear = { rid: uint, why: "answered_pc"/"answered_dev"/"expired"/"cancelled" }
```
The device must show approve/deny **only if** they are in `acts`. The daemon includes them only for prompts the agent itself raised (DESIGN Q6).

## 5. Commands (device → daemon)

```cddl
cmd = {
  op: "focus"/"ack"/"snooze"/"approve"/"deny"/"reply"/"cancel"/"detail"/"notify_done"/"quiet",
  ? sid: uint,
  ? rid: uint,               ; required for approve/deny/reply
  ? arg: any,                ; snooze minutes, reply option index, quiet duration...
  hold_ms?: uint             ; for approve: measured hold-to-confirm duration (daemon enforces ≥ 1000)
}
result = { ok: bool, ? err: "stale"/"denied"/"unsupported"/"no_window"/"timeout", ? msg: tstr }
```
- `focus`: the daemon runs the FocusDriver for that session. On `no_window`, the device shows "Open terminal not found".
- `detail`: the daemon replies with `detail { rid|sid, text }`, which holds the full text. This is sent only when privacy ≠ `meta`.
- `approve`/`deny` on a `rid` that has already been cleared returns `err: "stale"`, so a race against the terminal answer is safe.

## 6. Audio (v3)
```
audio_start { aid, codec: "adpcm16k"/"opus16k", sid? }   device → daemon
audio       { aid, seq, data: bstr }                       device → daemon (≤ 240 B each)
audio_end   { aid }                                        device → daemon
transcript  { aid, text, autosend_ms: 3000 }               daemon → device
cmd { op: "reply", sid, arg: { aid } } / cmd { op: "cancel", arg: { aid } }
```

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
