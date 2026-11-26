// Glance's glyph key: the state glyphs, the two key hints and the agent marks, drawn from the
// firmware's own 4 bpp font bitmaps (facts.glyphs, read from the LVGL font by extract.py), so the
// key shows the pixels the watch draws, not a lookalike.
/**
 * One glyph of facts.glyphs.icons: its ink box, offset and alpha per pixel (one hex digit, 0-15).
 * @typedef {{ cp: string, w: number, h: number, x: number, y: number, adv: number, a: string }} Glyph
 */
/**
 * What the key reads of facts.glyphs.
 * @typedef {{ px: number, icons: Record<string, Glyph>, states: Record<string, string | Record<string, string>>, keys: { a: { icon: string, color: string }, b: { icon: string, color: string }, r_px: number, ring_w_px: number }, agents: { id: string, name: string, icon: string }[] }} GlyphFacts
 */
import { STATE_ROWS, rowColor, stateLabel } from '../../gestures/actions.js';

const NS = 'http://www.w3.org/2000/svg';
// states are named as the firmware names them (facts.states, via stateLabel); keys in two words
const KEY_WORD = { a: 'Terminal forward', b: 'Next session' };

/**
 * The SVG paths of a glyph: one path per alpha level, each a set of one-pixel-high runs, laid out
 * in a `size`-px square with the ink centred (ui_glyph_draw centres the ink, not the line box).
 * @param {Glyph} g the glyph
 * @param {number} size the square's side, in font pixels
 * @returns {string} SVG markup
 */
export function glyphPaths(g, size) {
  /** @type {string[]} */
  const runs = Array.from({ length: 16 }, () => '');
  const ox = (size - g.w) / 2,
    oy = (size - g.h) / 2;
  for (let y = 0; y < g.h; y++) {
    let x = 0;
    while (x < g.w) {
      const v = parseInt(g.a[y * g.w + x], 16);
      let n = 1;
      while (x + n < g.w && g.a[y * g.w + x + n] === g.a[y * g.w + x]) n++;
      if (v) runs[v] += `M${x + ox} ${y + oy}h${n}v1h-${n}z`;
      x += n;
    }
  }
  return runs
    .map((d, v) => (d ? `<path d="${d}" fill-opacity="${(v / 15).toFixed(3)}"/>` : ''))
    .join('');
}

/**
 * A glyph as an inline SVG in `color` (any CSS colour), or a key hint: the glyph at its key size
 * inside the thin ring (ui_kit.c disc_draw, key form).
 * @param {GlyphFacts} gf facts.glyphs
 * @param {string} icon the ICON_* name
 * @param {string} color the ink
 * @param {boolean} [key] draw the key-hint ring round it
 * @returns {SVGSVGElement} the SVG
 */
function glyphSvg(gf, icon, color, key = false) {
  const g = gf.icons[icon];
  const size = gf.px;
  const svg = /** @type {SVGSVGElement} */ (document.createElementNS(NS, 'svg'));
  svg.setAttribute('viewBox', `0 0 ${size} ${size}`);
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('class', 'glyph-key__svg');
  svg.style.color = color;
  if (!g) return svg;
  if (key) {
    // the ring is r_px outside, ring_w_px wide; the glyph is drawn at the ring's radius in px
    const { r_px: r, ring_w_px: w } = gf.keys;
    const s = (size - 2) / (2 * r); // the ring fills the square, less a pixel of air
    const k = r / size; // a 20 px glyph in the 44 px bitmap's place
    svg.innerHTML =
      `<circle cx="${size / 2}" cy="${size / 2}" r="${(r - w / 2) * s}" fill="none" stroke="currentColor" stroke-width="${w * s}"/>` +
      `<g fill="currentColor" transform="translate(${(size * (1 - k * s)) / 2} ${(size * (1 - k * s)) / 2}) scale(${k * s})">${glyphPaths(g, size)}</g>`;
  } else {
    svg.innerHTML = `<g fill="currentColor">${glyphPaths(g, size)}</g>`;
  }
  return svg;
}

/**
 * The state glyph the firmware draws for a Glance state row (theme_state_glyph).
 * @param {GlyphFacts} gf facts.glyphs
 * @param {string[]} fact the row's [state, sub] path into facts.states
 * @returns {string} the ICON_* name
 */
function stateIcon(gf, fact) {
  const [state, sub] = fact;
  const e = gf.states[state];
  if (typeof e === 'string') return e;
  if (e && sub != null && typeof e[sub] === 'string') return e[sub];
  return /** @type {string} */ (gf.states['*']);
}

/**
 * Fills the glyph-key slot: three groups (states, keys, agents), each glyph with a word.
 * @param {HTMLElement | null} slot the `[data-slot="glyphs"]` element
 * @param {{ glyphs?: GlyphFacts, palette: Record<string, string> }} facts the page's facts
 */
export function mountGlyphKey(slot, facts) {
  const gf = facts.glyphs;
  if (!slot || !gf) return;
  /**
   * @param {string} label the group's name, for screen readers
   * @param {{ svg: SVGSVGElement, word: string }[]} items its glyphs
   * @returns {HTMLUListElement} the group
   */
  const group = (label, items) => {
    const ul = document.createElement('ul');
    ul.className = 'glyph-key__group';
    ul.setAttribute('aria-label', label);
    for (const it of items) {
      const li = document.createElement('li');
      li.className = 'glyph-key__item';
      const w = document.createElement('span');
      w.textContent = it.word;
      li.append(it.svg, w);
      ul.append(li);
    }
    return ul;
  };
  const states = STATE_ROWS.map((row) => ({
    svg: glyphSvg(gf, stateIcon(gf, row.fact), rowColor(row, facts)),
    word: stateLabel(row, facts),
  }));
  const keys = /** @type {const} */ (['a', 'b']).map((k) => ({
    svg: glyphSvg(gf, gf.keys[k].icon, gf.keys[k].color, true),
    word: KEY_WORD[k],
  }));
  const agents = gf.agents.map((a) => ({
    svg: glyphSvg(gf, a.icon, 'var(--paper)'),
    word: a.name,
  }));
  const title = document.createElement('h3');
  title.className = 'chapter__sub';
  title.textContent = 'Glyph key';
  slot.append(
    title,
    group('States', states),
    group('Key hints: A, B', keys),
    group('Agents', agents),
  );
}
