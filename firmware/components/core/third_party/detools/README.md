# detools patch decoder (vendored)

The decoder side of [detools](https://github.com/eerimoq/detools) 0.53.1
(commit a7cc5383e60959ba5d3f87da9bd7aa04aa39e3c8, `c/`), used by `core/ota.c` for delta firmware updates
([FIRMWARE.md](../../../../../docs/design/FIRMWARE.md) §9, PROTOCOL.md §6c). Licence: BSD-2-Clause, see
`LICENSE`. `heatshrink/` comes with detools from [heatshrink](https://github.com/atomicobject/heatshrink),
ISC licence, see `heatshrink/LICENSE`.

Files: `detools.c`, `detools.h`, `heatshrink/heatshrink_decoder.{c,h}`, `heatshrink_common.h`,
`heatshrink_config.h`. The only change to upstream: `heatshrink_config.h` guards the three
`HEATSHRINK_STATIC_*` defines with `#ifndef`, so the build can set them.

Configuration (compile definitions in `core/CMakeLists.txt`, applied to all of core because `ota.c` holds the
decoder struct): no file IO; heatshrink only (no none, LZMA or CRLE compression); static heatshrink buffers,
`HEATSHRINK_STATIC_WINDOW_BITS=11`, `HEATSHRINK_STATIC_LOOKAHEAD_BITS=10` (a patch whose header names other
sizes is refused), `HEATSHRINK_STATIC_INPUT_BUFFER_SIZE=32` (detools sinks one byte at a time).
`struct detools_apply_patch_t` is then 2288 bytes (host build; 2094 of them the heatshrink decoder).
