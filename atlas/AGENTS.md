# Agent guide: atlas

The Atlas: the static visual explainer served from the root `index.html`, with JS modules, facts
extracted from source and the core built to WebAssembly. See [README.md](README.md) and
[CONTRACT.md](CONTRACT.md) (module owners and the `facts.json` schema).

Follow [docs/conventions/js.md](../docs/conventions/js.md) (`tools/extract.py`:
[python.md](../docs/conventions/python.md)).

## Build and test

```sh
scripts/check atlas
# = python3 atlas/tools/extract.py --check && node --test atlas/test/*.test.mjs
scripts/atlas --open        # serve index.html and atlas/ on localhost:8700
atlas/wasm/build.sh         # rebuild vendor/watch.{js,wasm} (needs Emscripten)
```

## Where new code goes

- A module goes in the area its owner holds in `CONTRACT.md`; cross-area calls go through the shared
  `ctx` and the bus (S-TOOL-02).
- A test: `test/<area>_<what>.test.mjs` with `node:test` (JS-18).
- A constant the page needs is a fact: add it to `tools/extract.py`, never hand-copy it.

## Gotchas

- **After any firmware edit, run `python3 atlas/tools/extract.py` and commit `data/facts.json`**
  (and `css/tokens.generated.css`); `--check` fails otherwise. It also fails on an unlisted
  message type, so a protocol change needs it too.
- No npm runtime dependencies and no CDN; vendored code goes in `vendor/` with its licence.
- Modules and WASM do not load from `file://`: serve the page with `scripts/atlas`.
- `vendor/watch.{js,wasm}` is generated and is not rebuilt by `scripts/check`; rebuild it when the
  watch in the page must match a core change.
