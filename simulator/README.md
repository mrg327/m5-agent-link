# Desktop simulator

Runs the firmware `core` (LVGL UI, link manager, gestures, debug) on a PC. `simulator/src` is an
SDL2 implementation of `firmware/components/agentlink_hal/include/hal.h`; nothing in `core` knows the difference.

## Build and test

```sh
brew install sdl2 ninja                                 # macOS (sdl2-compat is fine)
sudo apt-get install -y libsdl2-dev ninja-build         # Debian/Ubuntu
cmake -S simulator -B build/sim -G Ninja
cmake --build build/sim
ctest --test-dir build/sim   # 12 protocol vectors, gesture recognizer, scripted headless UI run
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

One command per line, `#` comments. Input is replayed in real time through the same paths as real
input, so gesture recognition is exercised.

```
wait MS | tap X Y | hold X Y MS | press X Y | release | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long|double] | chord
scrub DEG0 DEG1 [MS [RADIUS]] | session SID STATE REPO [BRANCH [SUMMARY]] | drop SID
lift | shake | connect | disconnect | rx HEX | ui | shot FILE.png
attention RID SID approval|question|error|done ACTS GRANT_MASK TITLE|TEXT|OPT|OPT...
result ok | result err stale|denied|... | clear RID [WHY]
expect_screen NAME | expect_label TEXT | expect_no_label TEXT | quit
```

`rx` feeds a CBOR envelope (e.g. `cbor_hex` from `protocol/vectors`) as if the daemon sent it;
`attention`, `result` (answers the device's last command) and `clear` build such envelopes with the
firmware's own codec. `connect` fakes the transport being up for `--no-ws` runs. A failed `expect_*`
exits with code 1. `shot` writes a PNG of the framebuffer (round mask applied); `press` without a
`release` keeps the finger down, so a hold can be photographed mid-way.

## Notes

- Single thread: the main loop pumps SDL and the WebSocket between `core_step()` calls.
- The WebSocket client is hand-written (`sim_ws.c`): binary frames, ping/pong, close; no TLS.
