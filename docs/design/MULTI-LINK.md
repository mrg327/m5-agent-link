# m5-agent-link: one watch, several machines

Status: **phase 1 built** on branch `multilink` (issue `20261114-101b`). The spike numbers in §9 were measured on the
watch. Everything else is a proposal until the phases in §10 land. Items marked **[ask]** need the
user's call.

## 1. Goal

Two machines, each running `agentlinkd` (for example the Mac and the Linux laptop), are in BLE range of
one watch at the same time. Both stay linked:
- Sessions from both machines show on the ring together.
- Every answer, reply, focus, scroll, dictation and keyboard stroke goes back to the machine that owns
  the session.

Today the first machine to connect owns the watch, and the other sees nothing.

Out of scope:
- A daemon relaying another machine's sessions. [REMOTE.md](REMOTE.md) already does that over SSH,
  and it stays the answer for a machine that is not in BLE range.
- Two daemons on one machine.
- More than two machines (§3).

## 2. Why it does not work today

| Layer | Today | File |
|---|---|---|
| BLE controller | `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`, `CONFIG_BT_CTRL_BLE_MAX_ACT=3` | `sdkconfig.defaults` |
| Advertising | Stops when a central connects and restarts only on disconnect, so a second machine never sees the watch | `link_ble.cpp` |
| BLE HAL | One `s_conn`, one `s_subscribed`, one reassembly buffer (`s_rasm`, 4 KB). A second connection's subscribe is adopted as *the* link | `link_ble.cpp` |
| Link layer | One active link (`g_link.kind`). The first `welcome` claims it, and frames from other transports are dropped. USB is preferred: a USB `welcome` takes over | `link.c` |
| Wire ids | Each daemon numbers its sessions from 1 (`registry.rs` `next_sid`). Its request ids start at a random number in 1..2^31 and use the whole u32 range (`broker.rs` `first_rid`). Two daemons' `sid`s clash at once; `rid`s clash rarely | daemon |
| Model | One peer: `peer_minor`, the host name, cfg, quick replies, `pointer`, plan usage. A `snapshot` replaces every session | `model.c`, `link_rx.c` |

One fact makes this tractable: the protocol is already per link. Ids, `hello`/`welcome`, `snapshot`
and `ping` are all scoped to one link. So the watch can run one protocol instance per linked daemon
and merge them in its model. **No daemon change is required for the basic feature.** An old daemon
links as one of the two, unaware of the other.

## 3. How many links

**Two linked daemons**, over any mix of transports: BLE + BLE, USB + BLE, or WS + WS in the simulator.

- **BLE connections: 2** (`MAX_CONNECTIONS=2`, `BLE_MAX_ACT=4`: two connections, advertising and one
  spare). That covers the stated case (Mac + Linux laptop). A further connection costs little memory
  (1.1 KB of internal heap, §9). What it does cost is a share of the radio schedule and battery.
- **Decided (user, 2026-11-14): two is enough.** Nothing below is tied to two except the constant and
  the UI's colour pair.

## 4. Watch design

### 4.1 Ports and slots

- **Port:** where a frame came from. Today the HAL's `hal_link_kind_t` is the port. It grows from
  USB, BLE and WS to USB, BLE0, WS and BLE1. The existing values stay; BLE1 is new with value 3. The
  BLE HAL keeps `s_conn`, `s_subscribed`, the reassembly buffer, the MTU and the interval state per
  connection. The reassembly buffers move to PSRAM. `hal_link_send(port, …)` notifies that
  connection only.
- **Slot:** one linked daemon, `link_slot_t g_slots[2]`. A slot holds what `g_link` holds today
  (port, welcomed, `tx_id`, `last_rx_id`, pending commands, activity and hello times). It also takes
  what the model now keeps for the one peer: `peer_minor`, host id, name and os, cfg, quick replies,
  `pointer` and the latest plan usage.
- **Handshake:** `hello` goes out on every port that is up and not yet in a slot. A `welcome` on a
  free port takes a free slot.
- **One slot per machine.** A slot is keyed by the welcome's `host.id`, which today is a hash of the
  hostname.
  - A welcome from a host that already holds a slot moves that slot to the new port. The ids reset
    and a snapshot follows, as today.
  - If the old port is BLE, the watch ends that connection (`ble_gap_terminate`) so it does not hold
    a radio slot for nothing.
  - This is the old *USB is preferred* rule restated per machine. Plugging the Mac in moves the Mac's
    slot to USB and leaves the Linux laptop's BLE slot alone.
- **Both slots full:**
  - A welcome from a third host on BLE is ignored, and the watch ends that connection.
  - **[ask]** A third host on USB: USB means "this one, now", so it takes the slot that is *not*
    primary (§5). The alternative is to ignore it like BLE.
- **Losing a port** drops its slot: that slot's sessions and cards go (§4.3). The other slot is
  untouched. Today losing the link restarts the whole cycle.

### 4.2 Ids

The model and UI keep using one `uint32_t` sid and rid. The link layer translates between wire ids
and the watch's own ids.

- **sid: packed.** The watch's sid is `slot << 28 | wire sid`. Slot 0's ids are unchanged, so with
  one daemon nothing differs from today, and the simulator's existing scripts still pass.
  - Daemon sids count from 1. A wire sid ≥ 2^28 is dropped with a log line; it would take 268 M
    sessions in one daemon run.
  - Outgoing, the sid's top bits pick the slot, and the low bits go on the wire.
- **rid: mapped.** Daemon rids are random 31-bit starts that wrap at 2^32, so they cannot be packed.
  - The link keeps a table of 16 entries: watch rid ↔ (slot, wire rid). The watch numbers its rids
    from 1.
  - An entry is made by `attention`, `clear` or `detail` naming an unknown (slot, rid). It is freed
    LRU; attention holds at most 4, so 16 leaves room for a late `result` or `clear`.
  - A command for a rid no longer in the table is not sent. The card has already gone.
- **Ids made on the watch** (`aid`, `kid`, `seq`): already unique on the watch. They go to the slot
  of the session they belong to.
- **Ids scoped by a sid** (`lm`, the graph): their sid already picks the slot.

### 4.3 Model

- `snapshot` replaces **that slot's** sessions only (`model_replace_slot`), and its `away` events
  join the list.
- A slot that drops removes its sessions after a **30 s grace** (decided, user, 2026-11-14). A BLE blip should
  not empty the ring. Today a lost link keeps the cards until the next snapshot. With two slots, that
  would leave the dropped machine's stale cards beside live ones.
- `MODEL_MAX_SESSIONS` (32) and `MODEL_MAX_ATTENTION` (4) are shared. Attention rises to 6, so one
  busy machine cannot push the other's requests off the watch. Each attention is about 1.4 KB in
  PSRAM.
- `session.host` and `hn` keep their REMOTE.md meaning *within* a slot. The slot is a new field
  beside them.

### 4.4 Commands

| Traffic | Goes to |
|---|---|
| Anything with a sid or rid: `approve`, `deny`, `reply`, `answer`, `focus`, `scroll`, `type`, `dictate`, `detail`, `graph`, `rerun`, `unqueue`, `snooze`, `ack`, `notify_done`, `cancel`, `kb_*`, `audio_*` | The session's slot |
| `pointer` (trackpad) | The trackpad's target machine (§6), primary by default. Offered only by slots whose welcome says `pointer` |
| `quiet` | Every slot. Each daemon echoes `cfg`, and the primary's is applied (§5) |
| `fw_ack` | The slot that sent `fw_begin` |
| `power` report, unsolicited `log` | Primary |
| `pong`, `ui_state`, screenshot chunks | The slot that asked |
| Battery Level notify (GATT) | Every subscribed BLE connection |

## 5. Watch-wide traffic: the primary

Some state belongs to the watch, not to one session. One slot is **primary**: the first to be
welcomed. When it drops, the other slot becomes primary.

| Item | Rule |
|---|---|
| RTC time | Set by the primary's welcome only. A second machine's clock never moves the watch |
| `cfg.alert`, `quiet_until` (sound, buzz) | The primary's |
| `cfg.privacy` | Per slot. Each daemon already filters what it sends; the watch uses its own slot's value for that slot's cards |
| Plan usage gauges | **[ask]** The primary's, or the session's own slot on its card. The usage card is watch-wide today |
| Power reports | Primary only. One CSV, one fit; the other machine's `power-reports.csv` gets nothing new |
| OTA | **Any** slot may start one. `ota.c` already refuses a second `fw_begin` with `busy`, and it now names the slot it is talking to |
| Debug (`log` events) | Primary; answers to a request go to the asker |

### OTA with two machines

- **Delta base:** each daemon picks it from its own cache by the version in `hello`. A machine
  without that base image sends the whole image; nothing changes.
- **Restart:** the restart drops both links. Both daemons reconnect, and only the one that started
  the update is waiting for the new version.
- **Signing key:** the watch accepts one signing key, so both machines must sign with it
  (FIRMWARE.md §9). This is a live problem already: the watch now carries the Linux laptop's key, and
  the Mac's updates are refused.
- **Radio:** during the transfer the watch asks the *other* BLE connection for a slow interval
  (100 ms, latency 0), and restores it after. The OTA link keeps the 7.5 ms bulk interval.

## 6. UI (glyphs over words)

With one machine linked nothing changes on screen. With two:
- **Machine mark.** Each slot has a mark: the OS glyph from the welcome's `host.os` (Apple, Linux,
  Windows), drawn in the slot's colour (slot 0 the current accent, slot 1 a second hue). Two Macs
  differ by colour alone.
- **Ring:** a session's segment carries its slot's colour as a thin outer tick (decided, user,
  2026-11-14), not a tint of the whole segment.
- **Card:** the mark sits next to the repo, where REMOTE.md's host name goes today. A remote session
  keeps its `hn` as well, so the card reads *this daemon's machine → remote host*.
- **Link status:** two marks instead of one link glyph. A dropped machine's mark greys out until its
  grace runs out.
- **Trackpad:** the target machine's mark at the top. A tap on it switches to the other machine, if
  that one offers the pointer.
- **Welcome toast:** the arriving machine's mark, no text.

## 7. Daemon changes

None are required. These are worth having:
1. **Proto 0.23: the hello says `links: 2`.** A daemon then knows the watch is shared:
   - `agentlink status` shows "shared with 1 other machine". The watch would need to say which;
     **[ask]** whether it is worth a message.
   - The OTA progress line mentions that the other machine will lose the watch for the restart.
2. **Back off when turned away.** When both slots are full, the watch ends a third machine's
   connection. That daemon then reconnects, and the watch turns it away again. Within
   `RESCAN` that is a loop of connects, each costing both radios. The daemon should wait longer after
   a connection that ended before a welcome went out. **[unverified]** The exact retry pace of
   `ble.rs` still needs checking.
3. **Host id.** The hash of the hostname is stable but weak: two machines with the same hostname
   would share a slot. Optionally, add the machine id (`/etc/machine-id`, `IOPlatformUUID`,
   `MachineGuid`) to the hash.

## 8. Security

Nothing new in kind: there is still no bonding and no encryption (PROTOCOL.md §1). Two things change
in degree:
- The watch now advertises while linked, so a stranger nearby can take the free slot at any time,
  not only when the watch is idle. That central could show cards and receive the answers to its own
  cards. It never sees the other machine's sessions, because each slot gets only its own daemon's
  traffic. An OTA from it still needs the signing key.
- The machine mark (§6) makes a stranger's cards visible as a third colour or OS.

Recommendation: hold this until §8 of PROTOCOL.md (LESC pairing) lands, or add an **allowlist of
host ids** first. A host is allowed once it has linked over USB, or once confirmed with a hold on the
watch. **[ask]**

## 9. Spike: measured on the watch

A build of `main` + `MAX_CONNECTIONS=2`, `BLE_MAX_ACT=4`, advertising kept on after the first
connect, with internal-heap logging around the BLE init (worktree, not committed):

| Measured 2026-11-13, linked to the Linux laptop over BLE | 1 connection (today) | 2 connections |
|---|---|---|
| Static DIRAM free (`check_dram`) | 181,905 B | 181,905 B |
| Internal heap free before `nimble_port_init` | 94,087 B | 94,087 B |
| Internal heap free after `nimble_port_init` | 62,715 B | 61,611 B (−1,104) |
| Internal heap free after sync | 58,223 B | 57,119 B |
| Internal heap free once linked | 58,179 B | 57,075 B |
| Largest internal block | 31,744 B | 31,744 B |
| Advertising while linked | no | yes: this laptop's scan saw the watch's advertisements while its daemon held the link |

- **Memory does not limit this.** The second connection costs 1.1 KB of internal heap. The
  controller allocates it at runtime, so static DIRAM does not change.
- **The limits are the radio schedule and the battery (§3).** Phase 5 measures both.
- **Not yet tried: a second central actually connected.** That needs the Mac in range with its
  daemon running, and it is the first check of phase 1.
- **Phase 1 sketch, from the spike:**
  - On the first connect: `advertise()` again.
  - On a connect while `s_conn` is set: keep it as the second connection rather than adopting it.
  - On subscribe: per connection.
  - On disconnect: per connection, then `advertise()`.
  - The NimBLE events already name the connection handle.

## 10. Phases

1. **BLE HAL with two connections.**
   - Per-connection state, advertising while a slot is free, and the port enum.
   - The link layer still runs one slot; the second connection only gets hellos.
   - Checks: `check_dram`, the boot log, and two centrals connected (the Mac and this laptop).
2. **Link slots, id translation, per-slot model.**
   - Covers §4.1-4.4, §5 and the OTA routing.
   - Simulator: a second WS port (`--ws2 PORT`, the daemon's `AGENTLINK_WS` on another port) so two
     daemons with separate `AGENTLINK_HOME`s link to one simulator. Script commands inject frames
     per slot.
   - Tests: sid clash, rid clash, snapshot per slot, drop with grace, one host moving to USB, slots
     full, OTA from slot 1, primary handover.
3. **UI:** machine marks, ring ticks, link status, trackpad target (§6). Contact sheets for the user
   to pick from, as for UI v3.
4. **Daemon:** proto 0.23 `links`, back-off when ignored, host id (§7).
5. **Live:**
   - The Mac and the Linux laptop together for a day.
   - Power reports with one and two links, to give the battery cost of the second link at its idle
     interval.
   - OTA from each machine while the other stays linked, and its throughput against the one-link
     figure.
