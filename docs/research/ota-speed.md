# Faster OTA: delta updates and other levers

Scope: how to make `agentlink ota` faster for the 1.3 MB signed ESP32-S3 app image (issues 2047, ef2c). Before this
work an update sent the whole image: ~34 s over BLE from macOS, ~100 s from the Linux BCM4350C0 central after 2047,
~13 s over USB. Measured 2026-11-13. Items marked **[unverified]** come from secondary sources or memory.

## 1. Measurements on real images

All figures are for signed `agentlink.bin` images built from commits on `main`, in bytes.

| change | image | zlib -9 | identical 4 KB blocks | bsdiff+heatshrink 8/7 | bsdiff+heatshrink 11/10 | bsdiff+lzma |
|---|---|---|---|---|---|---|
| same source, rebuilt | 1314816 | - | - | 21119 | 4117 | - |
| small fix (95d85ba → a109c12) | 1314816 | 790248 | 70 / 322 | 35411 | 22290 | 15316 |
| a day of work (76a63d6 → 95d85ba) | 1314816 | 790246 | 12 / 322 | 135167 | 134204 | 94840 |

- **Sending only the changed blocks does not work for firmware.** One added function shifts every address after
  it, so only 22% of 4 KB blocks survive a small fix and 4% survive a day of work. A binary delta (bsdiff) encodes
  those shifts as "copy from the old image plus a small difference", which is why it gets 10-60x smaller.
- **Compression alone** (zlib) saves 40%. A delta of a day's work is 6x smaller than that.
- **Heatshrink parameters matter.** With a window of 8 bits and a lookahead of 7 bits, a match is at most 128 bytes.
  The 1.3 MB of zero difference bytes then cost ~20 KB, the bulk of a small patch. With 11/10 that floor falls to
  ~4 KB and the decoder needs a 2 KB window. Longer lookaheads (12+) make large changes worse.
- **LZMA** makes patches 30-60% smaller again but needs a much heavier decoder (liblzma, tens of KB of state).
  It is not worth it at these sizes.
- **Build timestamps** change about 30 bytes of the app description (date, time, ELF SHA) and the 384-byte
  signature changes anyway. `CONFIG_APP_REPRODUCIBLE_BUILD` would save little once the window is 11/10.

## 2. Options considered

1. **Delta OTA with detools (chosen, issue ef2c).** detools makes a sequential patch (bsdiff, heatshrink). The device
   streams it, reading the running app slot as the base and writing the rebuilt image into the spare slot. Espressif's
   `esp_delta_ota` component is a thin wrapper around the same decoder: `esp_delta_ota_feed_patch` takes chunks of
   any size, and a header holds the base image's SHA-256, checked against the running partition.
   ([esp_delta_ota](https://github.com/espressif/idf-extra-components/tree/master/esp_delta_ota),
   [API](https://github.com/espressif/idf-extra-components/blob/master/esp_delta_ota/include/esp_delta_ota.h),
   [patch generator](https://github.com/espressif/idf-extra-components/blob/master/esp_delta_ota/examples/https_delta_ota/tools/esp_delta_ota_patch_gen.py),
   [detools](https://github.com/eerimoq/detools)). Espressif only tests `esp_delta_ota` on the ESP32
   ([build rules](https://github.com/espressif/idf-extra-components/blob/master/esp_delta_ota/.build-test-rules.yml)),
   so this project vendors the detools C decoder in core, where the simulator tests it.
   - **Signing, rollback and resume are unchanged.** The rebuilt image goes through `esp_ota_write`/`esp_ota_end` as
     before, so the SHA-256 and RSA signature checks still apply. Rollback is not affected. A transfer interrupted by
     a dropped link resumes from RAM, as before.
   - detools also has an in-place patch type that can resume across reboots, but it overwrites the running slot. That
     defeats A/B rollback, so it is not used.
2. **Compressed whole image.** The ESP32-S3 ROM has miniz/tinfl, and heatshrink and LZ4 are small. This saves ~40%
   and is the fallback when no base is known **[unverified: ROM tinfl API on S3]**. It is not done yet. Once both
   ends keep their images, the delta covers almost every update.
3. **esp-bootloader-plus** applies compressed or delta images in the bootloader. It supports only the ESP32-C2 and
   ESP32-C3 and replaces the bootloader, so it is not an option here
   ([esp-bootloader-plus](https://github.com/espressif/esp-bootloader-plus)).
4. **Other delta tools** (JojoDiff/janpatch, MCUboot/Zephyr delta work, Mender, Memfault): none reuses more on
   ESP-IDF than detools does **[unverified]**.
5. **Transport tuning** helps every path, but less once patches are tens of KB:
   - Issue 2047 already asks for a fast connection interval during a transfer.
   - Batching acks, a larger window and erasing ahead of the write are still open.
   - Apple's accessory guidelines set a minimum interval of 15 ms **[unverified]**.
   - L2CAP connection-oriented channels exist in NimBLE and in CoreBluetooth (`CBL2CAPChannel`). BlueZ offers them
     only through raw sockets, and btleplug does not support them, so they are a lot of work for little gain now.
6. **Make deltas smaller.**
   - Builds can use `CONFIG_APP_REPRODUCIBLE_BUILD` and avoid `__DATE__`
     ([reproducible builds](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/reproducible-builds.html)).
   - Large static assets (fonts, sounds) could move into a data partition that is updated only when they change.
   - Neither is needed at today's sizes.

## 3. What remains after the delta

With a 22-135 KB patch, the radio carries data for under 1 s from macOS and a few seconds from Linux. The rest of an
update is fixed cost:
- reading and hashing the 1.3 MB base on the watch;
- erasing and writing the 1.3 MB spare slot as the image is rebuilt (the USB whole-image update, 13 s, is the upper
  bound for this);
- verifying the signature;
- restarting and reconnecting.

To go further, measure the `ms` in the watch's `ota_done` event, then consider erasing ahead of the write.

## 4. How the pieces fit (issue ef2c)

- **Protocol 0.21:** `fw_begin.patch {psize, base, bsize}`; chunk offsets count patch bytes (PROTOCOL.md §6c).
- **Base images:** the daemon keeps the images it sent, and every firmware build adds its signed image, in
  `~/.agentlink/fw/<sha256>.bin`. The daemon picks the base whose version matches the watch's `hello.fw`.
- **Safety:** the daemon applies its own patch and checks it before sending. The watch checks the base's SHA-256
  before it starts. On a mismatch the daemon sends the whole image.
