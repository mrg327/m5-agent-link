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
// Figtree with the tnum feature frozen in (tabular.py): equal-advance digits, nothing else changes.
mkdirSync(join(here, "tnum"), { recursive: true });
const fig = (w) => {
  const src = `node_modules/@fontsource/figtree/files/figtree-latin-${w}-normal.woff`;
  const dst = `tnum/figtree-${w}-tnum.ttf`;
  execFileSync("python3", ["tabular.py", src, dst], { stdio: "inherit", cwd: here });
  return dst;
};
const phDir = "node_modules/@phosphor-icons/web/src/fill";

const TEXT_RANGE = "0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026";
const NUM_RANGE = "0x20,0x25,0x2D,0x2E,0x30-0x3A"; // space % - . 0-9 :

// Phosphor name -> code point (UI-V3 §2.2). Checked against the package's own stylesheet.
const ICONS = {
  "arrows-clockwise": 0xe094, "chat-circle-dots": 0xe16c, "hand-palm": 0xe57e, "check-circle": 0xe184,
  "warning-circle": 0xe4e2, moon: 0xe330, "x-circle": 0xe4f8, plugs: 0xeb56,
  bluetooth: 0xe0da, usb: 0xe956, desktop: 0xe560, "battery-full": 0xe0c0, "battery-high": 0xe0c2,
  "battery-medium": 0xe0c6, "battery-low": 0xe0c4, "battery-warning": 0xe0c8, "battery-charging": 0xe0ba,
  microphone: 0xe326, keyboard: 0xe2d8, bell: 0xe0ce, "bell-slash": 0xe0d4, "speaker-high": 0xe44a,
  "speaker-slash": 0xe45a, "arrow-square-out": 0xe5de, pulse: 0xe000, "stop-circle": 0xe46e,
  "arrow-bend-up-left": 0xe024, "paper-plane-tilt": 0xe398, info: 0xe2ce, "arrow-line-down": 0xe05c,
  backspace: 0xe0ae, circle: 0xe18a, "git-fork": 0xe27e,
  // The glyph pass (GLYPHS.md): one glyph per word the screens used to spell out.
  sparkle: 0xe6a2, "airplane-tilt": 0xe5d6, cloud: 0xe1aa, "caret-right": 0xe13a, "arrow-left": 0xe058,
  check: 0xe182, x: 0xe4f6, "dots-three-outline": 0xe204, checks: 0xe53a, "bell-ringing": 0xe5e8,
  "arrow-clockwise": 0xe036, "arrow-counter-clockwise": 0xe038, "book-open": 0xe0e6, "hourglass-low": 0xe2b6,
  sun: 0xe472, "eye-closed": 0xe222, "eye-slash": 0xe224, stack: 0xe466, waveform: 0xe802,
  "cursor-click": 0xe7c8, "mouse-right-click": 0xe336, "mouse-scroll": 0xe332, "arrows-out-cardinal": 0xe0a4,
  "download-simple": 0xe20c, "calendar-blank": 0xe10a, "fast-forward": 0xe6a6, "shield-slash": 0xe410,
  "git-pull-request": 0xe282, files: 0xe710, "folder-dashed": 0xe8f8, prohibit: 0xe3de, "app-window": 0xe5da,
  "arrow-up": 0xe08e, "arrow-down": 0xe03e, "link-break": 0xe2e4, queue: 0xe6ac, "hand-tap": 0xec90,
  "arrow-right": 0xe06c, "caret-down": 0xe136,
};
// The GRAPH screen's node and lane glyphs (GRAPH.md §4): drawn inside a node disc, so 20 px only.
const GRAPH_ICONS = {
  dot: 0xecde, eye: 0xe220, "pencil-simple": 0xe3b4, "terminal-window": 0xeae8, "magnifying-glass": 0xe30c,
  globe: 0xe288, robot: 0xe762, "list-checks": 0xeadc, "git-commit": 0xe27a, flask: 0xe79e, plug: 0xe946,
  binoculars: 0xea64, lightning: 0xe2de, "seal-check": 0xe606, books: 0xe758, "map-trifold": 0xe31a,
};

const css = readFileSync(join(here, phDir, "style.css"), "utf8");
for (const [name, code] of Object.entries({ ...ICONS, ...GRAPH_ICONS })) {
  const m = css.match(new RegExp(`\\.ph-fill\\.ph-${name}:before\\s*{\\s*content:\\s*"\\\\([0-9a-f]+)"`));
  if (!m || parseInt(m[1], 16) !== code) throw new Error(`icon ${name}: expected ${code.toString(16)}, stylesheet says ${m && m[1]}`);
}
const hex = (o) => Object.values(o).map((c) => "0x" + c.toString(16)).join(",");
const ICON_RANGE = hex(ICONS);

function make(name, file, size, range, fallback) {
  const args = ["--font", file, "--range", range, "--size", String(size), "--bpp", "4", "--no-compress", "--format", "lvgl",
    "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", join(out, name + ".c")];
  if (fallback) args.push("--lv-fallback", fallback);
  execFileSync(conv, args, { stdio: "inherit", cwd: here });
}

mkdirSync(join(here, out), { recursive: true });
const ph = join(phDir, "Phosphor-Fill.ttf");
// Every size holds the graph glyphs too: the glyph pass reuses them (terminal, robot, eye, pencil, flask, globe).
for (const s of [20, 28, 44]) make(`font_phosphor_${s}`, ph, s, ICON_RANGE + "," + hex(GRAPH_ICONS));
// Each text font falls back to the icon font of the nearest size.
make("font_figtree_20_500", fig(500), 20, TEXT_RANGE, "font_phosphor_20");
make("font_figtree_20_400", fig(400), 20, TEXT_RANGE, "font_phosphor_20");
make("font_figtree_20_600", fig(600), 20, TEXT_RANGE, "font_phosphor_20");
make("font_figtree_24_600", fig(600), 24, TEXT_RANGE, "font_phosphor_28");
make("font_figtree_34_600", fig(600), 34, TEXT_RANGE, "font_phosphor_28");
make("font_figtree_56_600", fig(600), 56, NUM_RANGE);
