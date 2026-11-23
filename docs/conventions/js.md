---
status: accepted
owner: conventions
issue: 20261122-db53
last_reviewed: 2026-11-22
---

# JavaScript

JavaScript here is the Atlas (a browser app with Node tests), the design board's Node server, the
font generator, and the pi extension (TypeScript). The Google JS guide is
[no longer updated](https://google.github.io/styleguide/jsguide.html) and Airbnb's config has no
flat-config release, so our base is the current tool stack rather than a prose guide: ESLint flat
config with [`@eslint/js`](https://eslint.org/docs/latest/use/configure/) recommended,
[eslint-plugin-jsdoc](https://github.com/gajus/eslint-plugin-jsdoc) in its TypeScript-flavor mode,
[`tsc --checkJs`](https://www.typescriptlang.org/docs/handbook/intro-to-js-ts.html) with types
written in JSDoc, and [Prettier](https://prettier.io/docs/options) as the formatter. We keep two
Google rules: named exports only, and import paths with their extension.

## Inventory (2026-11-22)

44 first-party files, about 6,800 lines. `atlas/vendor/watch.js` is Emscripten output and is excluded
everywhere.

| Directory | Files | How it runs |
|---|---|---|
| `atlas/js/` | 30 `.js` (`bus`, `main`, `proto/`, `gestures/`, `stage/`, `flow/`, `replay/`, `sound/`) | Browser ES modules; `package.json` sets `"type": "module"`, Node `>=18` |
| `atlas/test/` | 6 `*.test.mjs` | `node --test atlas/test/*.test.mjs`; CI `atlas.yml` on Node 22 |
| `atlas/tools/` | `serve.mjs` | `node atlas/tools/serve.mjs ROOT PORT` (via `scripts/atlas`) |
| `tools/board/` | `board.mjs`, `server.mjs`, `drive.mjs`, `render.mjs` | Node; `board.mjs` is the MCP shim, it forks `server.mjs` |
| `firmware/tools/fonts/` | `gen.mjs` | `npm install && node gen.mjs` (its own `package.json` with pinned devDependencies) |
| `daemon/integrations/pi/` | `agentlink.ts`, `agentlink.test.ts` | TypeScript loaded by pi; `node --test daemon/integrations/pi` (needs Node's type stripping, 22.18 or newer) |

All files are ES modules already; nothing uses `require` or `module.exports`. The only default
export is the pi extension's entry point, which pi's API requires. Atlas code uses single quotes;
the board, fonts and pi code use double quotes. Indent is 2 spaces everywhere. There is no ESLint,
Prettier or tsconfig in the repository, and only one file has `@param` tags.

## Baseline (measured 2026-11-22 on scratch copies, nothing installed in the repository)

| Check | Result |
|---|---|
| `prettier@3.10.1 --check --print-width 100` | 42 of 44 files differ (both quote settings) |
| lines changed by that reformat, `singleQuote: true` | 1,433 |
| lines changed by that reformat, `singleQuote: false` | 2,113 |
| lines over 100 columns today | 604 |
| `tsc@5 --checkJs --strict --noEmit`, 42 `.js`/`.mjs` files, no `@types/node` | 1,314 errors: TS7006 (implicit `any` parameter) 722, TS7005 162, TS2339 111, TS7053 64, TS7034 56, TS2591 (Node globals, gone with `@types/node`) 45 |
| ESLint | not measured: it needs its plugins installed next to a config file, which phase 0 does not add |

Phase 3 (2026-11-22) measured both with the pinned tools; see [Lint baseline](#lint-baseline).

Single quotes win because they change a third fewer lines: most of the code is in the Atlas.

## Tools

**JS-1. Format with Prettier.** 100 columns to match C and Python, single quotes, semicolons, trailing
commas, 2-space indent. Prettier formats only `*.js`, `*.mjs` and `*.ts`; JSON vectors and Markdown
are not its job. The reformat is one commit, listed in `.git-blame-ignore-revs`. planned: phase 1
(`prettier --check`, blocking).

**JS-2. Lint with ESLint flat config.** `@eslint/js` recommended plus
`jsdoc.configs['flat/recommended-typescript-flavor-error']` and the few rules below. It runs on
every first-party `.js`, `.mjs` and `.cjs` file; existing findings are counted per (rule, file) in
a baseline that may only shrink. checked: scripts/check lint (lint_js.py, baseline 219).

**JS-3. Type-check with `tsc --noEmit`.** `checkJs` and `strict` over all `.js` and `.mjs` files;
types live in JSDoc, so nothing is rewritten in TypeScript. New files must have zero errors;
existing files keep an error count per (code, file) in the baseline that may only shrink. The pi
extension is checked the same way against pi's own types. A file may not add `// @ts-nocheck`
(review). checked: scripts/check lint (lint_js.py, baseline 1,240).

**JS-4. Tools are pinned.** Prettier is pinned by version in `scripts/check.d/format.sh`
(`npx --yes prettier@3.10.1`). ESLint and TypeScript are dev dependencies of
[`tools/lint/package.json`](../../tools/lint/README.md), exact versions with a committed lockfile,
installed by `npm ci --prefix tools/lint --ignore-scripts` into a gitignored `tools/lint/node_modules`
(lint_js.py does that itself when the lockfile changes). They live there, not in a root
`package.json`, so the repository root stays free of `node_modules` and the 200 MB of pi types are
fetched only by the lint step. `firmware/tools/fonts/package.json` stays separate because it pins the
font sources. checked: scripts/check lint (lint_js.py runs only the pinned binaries).

| Package | Version | Why |
|---|---|---|
| `eslint` | 10.14.0 | flat config only |
| `@eslint/js` | 10.0.1 | `recommended` |
| `eslint-plugin-jsdoc` | 65.4.3 | needs Node 22.22.2 or newer |
| `globals` | 17.14.0 | browser and Node globals |
| `typescript` | 6.0.3 | the last JavaScript-built compiler; 7.0 (native) is outside typescript-eslint's peer range, which eslint-plugin-jsdoc pulls in |
| `@types/node` | 22.20.5 | matches CI's Node 22 |
| `@earendil-works/pi-coding-agent` | 1.5.0 | pi's `ExtensionAPI` types for the pi extension |

### `.prettierrc.json` (repository root)

```json
{
  "printWidth": 100,
  "singleQuote": true
}
```

Prettier 3's defaults already give `semi: true`, `trailingComma: "all"`, `arrowParens: "always"` and
2-space indent. Prettier also reads `.editorconfig`; `.prettierrc.json` wins where they differ, so the
two must agree on indent for JS. `.prettierignore` lists `atlas/vendor/`, `**/node_modules/` and
`**/build/`.

### `eslint.config.mjs` (repository root)

The config is [`eslint.config.mjs`](../../eslint.config.mjs): `.mjs` because the root has no
`package.json` to make `.js` a module. Its plugins are loaded with `createRequire` from
`tools/lint/`, since ESLint resolves nothing from the config's own directory. It applies `@eslint/js`
recommended and `flat/recommended-typescript-flavor-error` to `*.js`/`*.mjs`, then the rules named in
JS-5, JS-7 and JS-10 to JS-15 (`no-var`, `prefer-const`, `eqeqeq` with `null: 'ignore'`, `camelcase`
with `properties: 'never'`, `new-cap`, `no-throw-literal`, `no-eval`, `no-new-func`,
`no-restricted-syntax` on `ExportDefaultDeclaration`, `jsdoc/require-jsdoc` with `publicOnly`),
browser globals for `atlas/js/` and Node globals for the tests and tools. It ignores `atlas/vendor/`,
`node_modules/`, `build/`, `firmware/managed_components/` and `daemon/integrations/pi/`.

The config file is the one place a default export is allowed outside pi: ESLint requires it. The pi
TypeScript files are left to `tsc` and Prettier; linting them would need `typescript-eslint`, which
we add only if more TypeScript arrives.

### `tsconfig.json` (repository root) and `daemon/integrations/pi/tsconfig.json`

The root [`tsconfig.json`](../../tsconfig.json) is `allowJs`, `checkJs`, `strict`, `noEmit`,
`module`/`moduleResolution` `nodenext`, `lib` es2023 + DOM, `types: ["node"]` (from
`tools/lint/node_modules/@types` through `typeRoots`), `skipLibCheck`, over `atlas/js`, `atlas/test`,
`atlas/tools`, `tools/board` and `firmware/tools/fonts/*.mjs`.

[`daemon/integrations/pi/tsconfig.json`](../../daemon/integrations/pi/tsconfig.json) is plain strict
TypeScript with `allowImportingTsExtensions`, `erasableSyntaxOnly` (Node's type stripping runs the
tests) and `verbatimModuleSyntax`. pi loads the file through jiti, not Node's resolver, so it uses
`module: preserve` with `moduleResolution: bundler`, and `paths` maps
`@earendil-works/pi-coding-agent` to the copy in `tools/lint/node_modules`. It has no errors today.

### Lint baseline

`scripts/check.d/baselines/lint_js.txt`, measured 2026-11-22 (235 (rule, file) pairs, 1,459
findings; 44 files). A run takes about 11 s on an idle machine once installed (ESLint and both `tsc`
projects in parallel), plus about 20 s for the first `npm ci`.

| Rule | Count | | Rule | Count |
|---|---|---|---|---|
| TS7006 implicit `any` parameter | 711 | | `new-cap` | 81 |
| TS7005 implicit `any` variable | 162 | | `jsdoc/require-jsdoc` | 51 |
| TS2339 unknown property | 123 | | `jsdoc/require-param` | 39 |
| TS7053 implicit `any` index | 67 | | `jsdoc/require-returns` | 20 |
| TS7034 | 56 | | `no-empty` | 11 |
| TS7031 | 35 | | `no-unused-vars` | 7 |
| TS18046 | 20 | | `prefer-const` | 5 |
| TS18047 | 17 | | `camelcase` | 2 |
| other TS codes (14) | 49 | | 3 others (one each) | 3 |
| **tsc total** | **1,240** | | **ESLint total** | **219** |

No `no-var`, `eqeqeq`, `no-throw-literal`, `no-eval`, `no-new-func` or `no-restricted-syntax`
findings exist, so those rules already block everywhere, as does any finding in the pi extension.

## Modules

**JS-5. ES modules only.** `import`/`export`, never `require` or `module.exports`. A `.js` file sits
under a `package.json` with `"type": "module"`; a Node script outside one uses `.mjs`. Without it,
Node 18 loaded `atlas/js/proto` as CommonJS and the tests failed (issue 20261109-f95e). checked:
`node --test` in CI; scripts/check lint (lint_js.py: ESLint `sourceType: 'module'`, tsc).

**JS-6. Relative imports carry their extension.** `import { crc16 } from './crc16.js'`, not
`'./crc16'`. Node built-ins use the `node:` prefix. checked: Node and browsers (an import without the
extension fails to load); scripts/check lint (lint_js.py, `tsc` with `nodenext`, baseline 0).

**JS-7. Named exports only.** No `export default`: a named export keeps one spelling for a thing
across every importer and lets search find it. Exceptions are files whose host demands a default
(the pi extension entry point, `eslint.config.mjs`); each gets a one-line comment saying so.
checked: scripts/check lint (lint_js.py, `no-restricted-syntax`, baseline 0).

```js
export function cobsEncode(data) { … }        // good
export default function (data) { … }          // bad
```

**JS-8. Modules have no side effects on import**, except a page's entry point (`atlas/js/main.js`)
and a script's `main`. Pure logic (codecs, classifiers, synths) stays free of DOM and Node APIs, so
`atlas/test` can import it directly. checked: review.

**JS-9. No circular imports.** A cycle means two modules are one; merge them or move the shared part
down. checked: review (planned: phase 3, `import-x/no-cycle` if review misses one).

## Code

**JS-10. Naming.** `lowerCamelCase` for functions, variables and methods; `UpperCamelCase` for classes
and typedefs; `CONSTANT_CASE` for module-level constants that never change (`MAX_PAYLOAD`). Files are
lowercase with `-` or `_` (`mock-daemon.js`, `facts_schema.test.mjs`). Protocol field names keep the
CDDL spelling inside object literals; see [protocol.md](protocol.md). checked: scripts/check lint
(lint_js.py, `camelcase` with `properties: 'never'` baseline 2, `new-cap` baseline 81); review for
file names.

**JS-11. `const` by default, `let` when reassigned, never `var`. Use `===`**, except `== null` to test
for both `null` and `undefined`. checked: scripts/check lint (lint_js.py: `no-var` 0, `prefer-const`
baseline 5, `eqeqeq` 0).

**JS-12. Throw `Error` objects with a prefix.** `throw new Error('cbor: trailing bytes')`: the
prefix names the module. Never throw strings. Async code awaits or returns every promise it starts.
checked: scripts/check lint (lint_js.py, `no-throw-literal`, baseline 0); review for promises.

**JS-13. No `eval`, `new Function` or `with`.** checked: scripts/check lint (lint_js.py, `no-eval`,
`no-new-func`, baseline 0). `with` is already a syntax error in modules.

**JS-14. Servers bind to localhost by default.** Any listening tool (`serve.mjs`, the board) defaults
to `127.0.0.1`; wider binds are opt-in through an environment variable named in the file header.
Never commit a hostname or address. checked: review.

## Types and docs

**JS-15. Every export has a JSDoc block with TypeScript-flavor types.** One summary sentence, then
`@param` and `@returns` with types; `@throws` when it throws. Types use TypeScript syntax inside the
braces (`Uint8Array | null`, `{start: number, end: number}[]`). Shared shapes go in an `@typedef`
next to the code that owns them, and other files import it with `import('./file.js').Name`.
checked: scripts/check lint (lint_js.py, `jsdoc/*` baseline 112, `tsc`), blocking in phase 4.

```js
/**
 * Splits one message into BLE notifications that fit the negotiated MTU.
 *
 * @param {Uint8Array} payload Encoded CBOR message.
 * @param {number} mtu Usable bytes per notification, header included.
 * @returns {Uint8Array[]} Fragments in send order; the first carries FIRST, the last LAST.
 * @throws {RangeError} If `mtu` is too small for the fragment header.
 */
export function fragment(payload, mtu) { … }
```

**JS-16. Comments.** `//` for implementation comments; `/** … */` only for JSDoc. A file starts with
a comment saying what it is and how it runs (as `server.mjs` does). Comments say why, or cite a
source (`daemon usb/frame.rs`, a doc section). `TODO(ISSUE-ID): what` links an issue. checked:
review.

**JS-17. Type escape hatches carry a reason.** Use `// @ts-expect-error <reason>`, never
`// @ts-ignore`, so the comment fails once the error is gone. `any` only at a JSON or wire boundary,
narrowed right away. checked: scripts/check lint (lint_js.py, `tsc`); review for `@ts-ignore`.

## Tests

**JS-18. Tests use `node:test` and `node:assert/strict`.** No test framework dependency. Files are
`<area>_<topic>.test.mjs` in `atlas/test/`, or `*.test.ts` next to the pi code. Tests that need
fixtures read them from `protocol/vectors/`, the same vectors the C and Rust tests use. checked:
`node --test` in CI (`atlas.yml`); review for the pi tests, which CI does not run yet.

**JS-19. Tests run on the oldest Node we claim.** The Atlas claims Node 18 (`engines`) but CI runs
22, and an 18-only failure has slipped through once (issue 20261109-f95e). Either add 18 to CI or
raise `engines`. checked: review.

## Running the checks

```sh
scripts/check lint           # ESLint + tsc (python3 scripts/check.d/lint_js.py)
node --test atlas/test/*.test.mjs
node --test daemon/integrations/pi
```

Until phase 1, check formatting from outside the repository so no `node_modules` appears in it:

```sh
npx --yes prettier@3 --no-config --print-width 100 --single-quote --check FILES
```
