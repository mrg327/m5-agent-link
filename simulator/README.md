# Desktop simulator

Runs the firmware `core` (LVGL UI, link manager, gestures, debug) on a PC. `simulator/src` is an
SDL2 implementation of `firmware/components/agentlink_hal/include/hal.h`; nothing in `core` knows the difference.

## Build and test

```sh
brew install sdl2 ninja                                 # macOS (sdl2-compat is fine)
sudo apt-get install -y libsdl2-dev ninja-build         # Debian/Ubuntu
cmake -S simulator -B build/sim -G Ninja
cmake --build build/sim
ctest --test-dir build/sim -j 16   # protocol vectors, unit tests, scripted headless UI runs (about 12 s; headless scripts run on a virtual clock)
```

LVGL v9.5.0 is fetched with FetchContent (config: `simulator/lv_conf.h`).

## Run

```sh
build/sim/agentlink_sim                    # window + WebSocket client to ws://127.0.0.1:7878
build/sim/agentlink_sim --headless         # no window; debug JSON lines on stdout
build/sim/agentlink_sim --no-ws --script simulator/scripts/selftest.txt --headless
```

Options: `--headless`, `--no-ws`, `--script FILE`, `--idle-off SECONDS` (display-off timeout).
Without a daemon it prints a `ws_retry` line every 2 s and keeps trying.

| Input | Meaning |
|---|---|
| mouse (left button) | touch: tap, double-tap, swipe, hold |
| `A` / `B` | buttons (hold for long press) |
| `L` | lift (down) / drop (up) |
| `S` | shake |
| `Esc` | quit |

The window is 466x466 with the area outside the circle painted grey. Everything the firmware does
is logged to stdout as one JSON object per line (`hal_debug_line`), including haptic patterns.

## Scripts

One command per line, `#` comments. Input is replayed on the script's timeline through the same paths
as real input, so gesture recognition is exercised. With `--headless --no-ws` the clock is virtual: it
advances only while the main loop sleeps, so a run takes as long as its drawing, not its `wait`s, and
a loaded machine cannot stretch a tap into a hold. It is virtual from before the core starts, and the
clock reads one hour at start-up whatever the host's uptime, so every run of a script sees the same clock and
draws the same frames. Set `AGENTLINK_SIM_REAL_CLOCK=1` for wall-clock time.
With a window or a WebSocket link the clock is real.

```text
wait MS | tap X Y | hold X Y MS | press X Y | release | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long|double|held|down|up] | chord
scrub DEG0 DEG1 [MS [RADIUS]] | session SID STATE REPO [BRANCH [SUMMARY]] | drop SID
lift | shake | connect | disconnect | rx HEX | ui | shot FILE.png
attention RID SID approval|question|error|done ACTS GRANT_MASK TITLE|TEXT|OPT|OPT...
result ok | result err stale|denied|... | clear RID [WHY]
graph SID KIND:STATE:LANE[:f][:m]... [la=K,K..] [drop=N] [gen=N] | graph off
expect_screen NAME | expect_label TEXT | expect_no_label TEXT | px_reset | expect_px_max PX | expect_px X Y RRGGBB | kit N | quit
frames [all] | expect_frame_px_max PX [AREAS]
touch DT X Y [X Y...] | bench start|stop|mark N|save FILE|expect KIND MIN | expect_cmd OP
pair NNNNNN | pair done | pair fail
```

`pair` plays a BLE pairing as the board reports it (PROTOCOL.md §8): the passkey to show, then the end.
`touch` and `bench` are the daemon's bench messages (BENCH.md, proto 0.24): a finger stroke played under the
touch read path, and the frame timeline the device sends back after `bench stop`. `expect_cmd OP` checks that
the device sent that command since the last `expect_cmd`.

`rx` feeds a CBOR envelope (e.g. `cbor_hex` from `protocol/vectors`) as if the daemon sent it;
`attention`, `result` (answers the device's last command) and `clear` build such envelopes with the
firmware's own codec. `graph` is a stand-in daemon for `cmd graph` (GRAPH.md): it answers every one for that session with
this graph (state `run`/`ok`/`fail`, `f` fork, `m` merge, `la` the lane kinds, lane 0 first). `connect` fakes the transport being up for `--no-ws` runs. A failed `expect_*`
exits with code 1. `shot` writes a PNG of the framebuffer (round mask applied); `press` without a
`release` keeps the finger down, so a hold can be photographed mid-way.
`kit N` shows page N of a gallery of the Sigil primitives (`ui_kit.h`, GLYPHS.md §3) on the top layer and
`kit 0` hides it (`scripts/kit_gallery.txt`); the gallery is compiled into the simulator only.
`px_reset` / `expect_px_max` bound the pixels LVGL flushed in between (`core_px_flushed()`); see
`perf_four_agents.txt` for the redraw budget with four busy agents. The log line also reports
`cpu_us` (host CPU inside `core_step`), informational only since it varies with machine load.
`frames` logs the frames since `px_reset` one by one (`"ev":"frames"`; `all` adds a `"ev":"frame"` line
each): how many, the pixels and the areas LVGL rendered per frame (p50, p95, max, after it joined them), the
48-row strips they take in the watch's buffers, the gap between frames and the host CPU per frame.
`expect_frame_px_max PX [AREAS]` fails when one frame since `px_reset` flushed more than PX pixels (or more
than AREAS areas); `expect_px_max` logs the same figures. BENCH.md §9 turns them into watch frame times.
`expect_px X Y RRGGBB` checks one framebuffer pixel, within 24 a channel (RGB565 and edge
anti-aliasing): `ring_pips.txt` tells the needs-you pip shapes apart with it.

Firmware update (OTA) commands: `fw_image SIZE [unsigned|badhdr|badsha]` synthesises a fake image
(0xE9 header, 0xE7 0x02 signature marker in the last 4 KiB) and its SHA-256; `fw_begin [VER]` sends
`fw_begin` for it; `fw_stop` sends `fw_begin` with size 0; `fw_send OFF LEN [COUNT]` sends COUNT
`fw_chunk` frames 20 ms apart (clamped to the image end); `battery PCT [chg]` sets the battery;
`skip MS` jumps the HAL clock forward (for the 30 s timeout and the 5 min rollback guard). The sim
OTA slot lives in RAM; `AGENTLINK_SIM_UNCONFIRMED=1` makes `hal_ota_unconfirmed()` true at boot.
`hal_restart()` logs a `restart` event and returns. See `scripts/ota_*.txt`.

## Notes

- Single thread: the main loop pumps SDL and the WebSocket between `core_step()` calls.
- The WebSocket client is hand-written (`sim_ws.c`): binary frames, ping/pong, close; no TLS.
