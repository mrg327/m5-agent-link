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

Single quotes win because they change a third fewer lines: most of the code is in the Atlas.

## Tools

**JS-1. Format with Prettier.** 100 columns to match C and Python, single quotes, semicolons, trailing
commas, 2-space indent. Prettier formats only `*.js`, `*.mjs` and `*.ts`; JSON vectors and Markdown
are not its job. The reformat is one commit, listed in `.git-blame-ignore-revs`. planned: phase 1
(`prettier --check`, blocking).

**JS-2. Lint with ESLint flat config.** `@eslint/js` recommended plus
`jsdoc.configs['flat/recommended-typescript-flavor-error']` and the few rules below. It runs on
changed lines, with a bulk-suppressions baseline (per file, per rule counts) that may only shrink.
planned: phase 3.

**JS-3. Type-check with `tsc --noEmit`.** `checkJs` and `strict` over all `.js` and `.mjs` files;
types live in JSDoc, so nothing is rewritten in TypeScript. New files must have zero errors;
existing files keep an error count in the baseline (1,314 today) that may only shrink. A file may
not add `// @ts-nocheck`. planned: phase 3.

**JS-4. Tools are pinned in one root `package.json`.** Dev dependencies only, with a committed
lockfile; `scripts/check` runs them through `npx --no-install`. `firmware/tools/fonts/package.json`
stays separate because it pins the font sources. planned: phase 1 (Prettier), phase 3 (ESLint,
TypeScript).

### Planned `.prettierrc.json` (repository root)

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

### Planned `eslint.config.js` (repository root)

```js
// ESLint config for the repository's JavaScript. docs/conventions/js.md
import js from '@eslint/js';
import jsdoc from 'eslint-plugin-jsdoc';
import globals from 'globals';

export default [
  { ignores: ['atlas/vendor/**', '**/node_modules/**', '**/build/**', 'firmware/managed_components/**'] },
  js.configs.recommended,
  jsdoc.configs['flat/recommended-typescript-flavor-error'],
  {
    files: ['**/*.js', '**/*.mjs'],
    languageOptions: { ecmaVersion: 2023, sourceType: 'module' },
    rules: {
      'no-var': 'error',
      'prefer-const': 'error',
      eqeqeq: ['error', 'always', { null: 'ignore' }],
      camelcase: ['error', { properties: 'never' }],
      'new-cap': 'error',
      'no-throw-literal': 'error',
      'no-eval': 'error',
      'no-new-func': 'error',
      'no-restricted-syntax': ['error', {
        selector: 'ExportDefaultDeclaration',
        message: 'Use a named export (docs/conventions/js.md JS-7).',
      }],
      'jsdoc/require-jsdoc': ['error', { publicOnly: true }],
    },
  },
  { files: ['atlas/js/**'], languageOptions: { globals: globals.browser } },
  {
    files: ['atlas/test/**', 'atlas/tools/**', 'tools/**', 'firmware/tools/**'],
    languageOptions: { globals: globals.node },
  },
];
```

This config file is the one place a default export is allowed: ESLint requires it. The pi
TypeScript files are left to `tsc` and Prettier; linting them would need `typescript-eslint`, which
we add only if more TypeScript arrives.

### Planned `tsconfig.json` (repository root)

```json
{
  "compilerOptions": {
    "allowJs": true,
    "checkJs": true,
    "noEmit": true,
    "strict": true,
    "target": "es2022",
    "module": "nodenext",
    "moduleResolution": "nodenext",
    "lib": ["es2023", "dom", "dom.iterable"],
    "types": ["node"],
    "skipLibCheck": true
  },
  "include": ["atlas/js", "atlas/test", "atlas/tools", "tools/board", "firmware/tools/fonts/*.mjs"],
  "exclude": ["atlas/vendor", "**/node_modules"]
}
```

`daemon/integrations/pi/` gets its own `tsconfig.json` (plain TypeScript, `allowImportingTsExtensions`,
pi's type package) because it builds against pi's API, not the browser.

## Modules

**JS-5. ES modules only.** `import`/`export`, never `require` or `module.exports`. A `.js` file sits
under a `package.json` with `"type": "module"`; a Node script outside one uses `.mjs`. Without it,
Node 18 loaded `atlas/js/proto` as CommonJS and the tests failed (issue 20261109-f95e). checked:
`node --test` in CI; planned: phase 3 (ESLint `sourceType: 'module'`).

**JS-6. Relative imports carry their extension.** `import { crc16 } from './crc16.js'`, not
`'./crc16'`. Node built-ins use the `node:` prefix. checked: Node and browsers (an import without the
extension fails to load); planned: phase 3 (`tsc` with `nodenext`).

**JS-7. Named exports only.** No `export default`: a named export keeps one spelling for a thing
across every importer and lets search find it. Exceptions are files whose host demands a default
(the pi extension entry point, `eslint.config.js`); each gets a one-line comment saying so.
planned: phase 3 (`no-restricted-syntax`).

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
CDDL spelling inside object literals; see [protocol.md](protocol.md). planned: phase 3 (`camelcase`
with `properties: 'never'`, `new-cap`); review for file names.

**JS-11. `const` by default, `let` when reassigned, never `var`. Use `===`**, except `== null` to test
for both `null` and `undefined`. planned: phase 3 (`no-var`, `prefer-const`, `eqeqeq`).

**JS-12. Throw `Error` objects with a prefix.** `throw new Error('cbor: trailing bytes')`: the
prefix names the module. Never throw strings. Async code awaits or returns every promise it starts.
planned: phase 3 (`no-throw-literal`); review for promises.

**JS-13. No `eval`, `new Function` or `with`.** checked: review; planned: phase 3 (`no-eval`,
`no-new-func`). `with` is already a syntax error in modules.

**JS-14. Servers bind to localhost by default.** Any listening tool (`serve.mjs`, the board) defaults
to `127.0.0.1`; wider binds are opt-in through an environment variable named in the file header.
Never commit a hostname or address. checked: review.

## Types and docs

**JS-15. Every export has a JSDoc block with TypeScript-flavor types.** One summary sentence, then
`@param` and `@returns` with types; `@throws` when it throws. Types use TypeScript syntax inside the
braces (`Uint8Array | null`, `{start: number, end: number}[]`). Shared shapes go in an `@typedef`
next to the code that owns them, and other files import it with `import('./file.js').Name`.
planned: phase 3 (`jsdoc/require-jsdoc` with `publicOnly`, `tsc`), blocking in phase 4.

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
narrowed right away. planned: phase 3 (`tsc`; `ban-ts-comment` by review).

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
scripts/check js             # planned: phase 1 (Prettier), phase 3 (ESLint, tsc)
node --test atlas/test/*.test.mjs
node --test daemon/integrations/pi
```

Until phase 1, check formatting from outside the repository so no `node_modules` appears in it:

```sh
npx --yes prettier@3 --no-config --print-width 100 --single-quote --check FILES
```
