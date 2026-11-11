// Generates the LVGL C fonts in ../../components/core/src/fonts (UI-V3 §2.1, §2.2).
//   npm install && node gen.mjs        (re-run only when a size, weight or icon changes; commit the output)
// 4 bpp, no compression: the glyphs live in flash and are read in place.
import { execFileSync } from "node:child_process";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
// Paths stay relative and the tool runs from this directory: lv_font_conv writes its arguments into each
// file header, and no absolute (user) path may end up in the repository.
const out = "../../components/core/src/fonts";
const conv = "node_modules/.bin/lv_font_conv";
const fig = (w) => `node_modules/@fontsource/figtree/files/figtree-latin-${w}-normal.woff`;
const phDir = "node_modules/@phosphor-icons/web/src/fill";

const TEXT_RANGE = "0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026";
const NUM_RANGE = "0x20,0x25,0x2D,0x2E,0x30-0x3A"; // space % - . 0-9 :

// Phosphor name -> code point (UI-V3 §2.2). Checked against the package's own stylesheet.
const ICONS = {
  "arrows-clockwise": 0xe094, "chat-circle-dots": 0xe16c, "hand-palm": 0xe57e, "check-circle": 0xe184,
  "warning-circle": 0xe4e2, moon: 0xe330, check: 0xe182, x: 0xe4f6, "x-circle": 0xe4f8, "wifi-slash": 0xe4f2,
  bluetooth: 0xe0da, usb: 0xe956, "plugs-connected": 0xeb5a, "battery-full": 0xe0c0, "battery-high": 0xe0c2,
  "battery-medium": 0xe0c6, "battery-low": 0xe0c4, "battery-warning": 0xe0c8, "battery-charging": 0xe0ba,
  microphone: 0xe326, keyboard: 0xe2d8, bell: 0xe0ce, "bell-slash": 0xe0d4, "speaker-high": 0xe44a,
  "speaker-slash": 0xe45a, "arrow-square-out": 0xe5de, pulse: 0xe000, "stop-circle": 0xe46e,
  "arrow-bend-up-left": 0xe024, "paper-plane-tilt": 0xe398, info: 0xe2ce, "arrow-line-down": 0xe05c,
  backspace: 0xe0ae, circle: 0xe18a,
};

const css = readFileSync(join(here, phDir, "style.css"), "utf8");
for (const [name, code] of Object.entries(ICONS)) {
  const m = css.match(new RegExp(`\\.ph-fill\\.ph-${name}:before\\s*{\\s*content:\\s*"\\\\([0-9a-f]+)"`));
  if (!m || parseInt(m[1], 16) !== code) throw new Error(`icon ${name}: expected ${code.toString(16)}, stylesheet says ${m && m[1]}`);
}
const ICON_RANGE = Object.values(ICONS).map((c) => "0x" + c.toString(16)).join(",");

function make(name, file, size, range, fallback) {
  const args = ["--font", file, "--range", range, "--size", String(size), "--bpp", "4", "--no-compress", "--format", "lvgl",
    "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", join(out, name + ".c")];
  if (fallback) args.push("--lv-fallback", fallback);
  execFileSync(conv, args, { stdio: "inherit", cwd: here });
}

mkdirSync(join(here, out), { recursive: true });
const ph = join(phDir, "Phosphor-Fill.ttf");
for (const s of [20, 28, 44]) make(`font_phosphor_${s}`, ph, s, ICON_RANGE);
// Each text font falls back to the icon font of the nearest size.
make("font_figtree_16_500", fig(500), 16, TEXT_RANGE, "font_phosphor_20");
make("font_figtree_20_400", fig(400), 20, TEXT_RANGE, "font_phosphor_20");
make("font_figtree_20_600", fig(600), 20, TEXT_RANGE, "font_phosphor_20");
make("font_figtree_24_600", fig(600), 24, TEXT_RANGE, "font_phosphor_28");
make("font_figtree_34_600", fig(600), 34, TEXT_RANGE, "font_phosphor_28");
make("font_figtree_56_600", fig(600), 56, NUM_RANGE);
