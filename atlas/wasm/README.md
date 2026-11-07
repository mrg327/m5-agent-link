# Watch firmware in WebAssembly

`firmware/components/core` + LVGL v9.5.0 (same as `simulator/`), compiled with Emscripten and
driven by `atlas/js/watch-runtime.js`. `web_hal.c` is the browser implementation of `hal.h`.

Rebuild (needs `brew install emscripten`, network for the first LVGL fetch):

    atlas/wasm/build.sh          # writes atlas/vendor/watch.js + watch.wasm
    LVGL_SRC=simulator/build/_deps/lvgl-src atlas/wasm/build.sh   # reuse a local LVGL checkout

- `lv_conf.h` is a verbatim copy of `simulator/lv_conf.h`; refresh it if the simulator's changes.
- Exports (`web_main.c`): `wasm_init/tick/pointer/button/motion/deliver/link_up/...`. JS owns the loop (rAF).
- Frames are raw CBOR envelopes, one per call, same as the simulator's WebSocket link. `reset()` in JS re-instantiates the module.
- Try it: `python3 -m http.server 8702` from the repo root, open `/atlas/wasm/test.html`.
