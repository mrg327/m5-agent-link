# lint

The pinned JavaScript lint tools: ESLint, `@eslint/js`, eslint-plugin-jsdoc, globals, TypeScript,
`@types/node`, and pi's type package for the pi extension. `package.json` names exact versions and
`package-lock.json` pins the whole tree. Nothing here is code; the configs are `eslint.config.mjs`,
`tsconfig.json` and `daemon/integrations/pi/tsconfig.json` ([js.md](../../docs/conventions/js.md)
JS-2 to JS-4).

```sh
npm ci --prefix tools/lint --ignore-scripts   # node_modules/ is gitignored
python3 scripts/check.d/lint_js.py            # installs if needed, runs eslint + tsc, checks the baseline
```

`lint_js.py` runs `npm ci` itself when `node_modules/` is missing or was installed from another
lockfile. Needs Node 22.22.2 or newer (eslint-plugin-jsdoc's floor).
