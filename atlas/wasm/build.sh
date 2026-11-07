#!/usr/bin/env bash
# Rebuild atlas/vendor/watch.{js,wasm} from firmware/components/core + LVGL. Needs Emscripten (brew install emscripten).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$here/build"
emcmake cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release ${LVGL_SRC:+-DFETCHCONTENT_SOURCE_DIR_LVGL="$LVGL_SRC"}
cmake --build "$build" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
mkdir -p "$here/../vendor"
cp "$build/watch.js" "$build/watch.wasm" "$here/../vendor/"
ls -l "$here/../vendor"/watch.*
