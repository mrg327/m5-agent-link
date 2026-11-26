# design/mote

The Mote's look in one file: `mote.toml` holds the colour, the halo's falloff, the levels, the breath,
the flare and the sizes; `gen.py` turns it into what the watch, the Atlas, the favicon and the CLI read
(issue 20261126-0163; the Mote is GLYPHS.md §3.14).

```sh
python3 design/mote/gen.py           # after editing mote.toml; commit the outputs with it
python3 design/mote/gen.py --check   # run by scripts/check structure: fails while an output is stale
```

| Path | Concern |
|---|---|
| `mote.toml` | The profile. Colour `#FFE2B8` at every hour; halo radius = core radius × 30 / 7; halo alpha = 0.40 × (1 − t)², t 0 at the core's edge, 1 at the halo's; levels lit 100 %, idle 45 %, night 30 %, ember 25 %; breath 4 s, halo alpha 60–100 %; sizes S, tucked, M, L, ember |
| `gen.py` | The generator (Python 3.11 stdlib, `tomllib`) |

Generated files (S-GEN-01: never edit them; change `mote.toml` or `gen.py`, rerun, commit both):

| File | What it holds |
|---|---|
| `firmware/components/core/src/mote_look.h` | `MOTE_LOOK_*` constants, the falloff table (65 alphas) and A8 halo sprites for S and tucked |
| `atlas/css/mote.generated.css` | `--mote-*` custom properties, the orb and halo gradients, `@keyframes mote-breath` |
| `atlas/img/mote.svg` | The orb at size M (60 px), halo as a radial gradient |
| `atlas/img/favicon-{16,32,96}.svg` | The favicon: a black disc, a grey rim, the orb at about 1:30 |
| `atlas/img/favicon-{32,180}.png` | The same favicon as PNG, for browsers without SVG icons and the iOS home screen |
| `daemon/crates/agentlink/src/mote.rs` | `MOTE_RGB` and `MOTE_ANSI256` (223) for the CLI's status mark |

The Atlas page reads the CSS and the favicons (the brand mark, the caption light and the hero drift,
issue 20261126-b92e) and the root `README.md` shows `favicon-96.svg`; the firmware renderer and the CLI
are wired to their outputs in later changes. The breath scales the halo's alpha only, so one sprite per
size serves every frame and the core stays crisp; the renderer draws the core over the sprite.
