// ESLint config for the repository's JavaScript (docs/conventions/js.md JS-2). Run it with
// `scripts/check lint`, or by hand: npm ci --prefix tools/lint && tools/lint/node_modules/.bin/eslint .
// The plugins live in tools/lint/node_modules (pinned by tools/lint/package-lock.json), so they
// are resolved from there rather than from this file's directory. The pi TypeScript is left to tsc.
import { createRequire } from 'node:module';

const require = createRequire(new URL('./tools/lint/package.json', import.meta.url));
const js = require('@eslint/js');
const jsdoc = require('eslint-plugin-jsdoc');
const globals = require('globals');

// ESLint requires a default export here (JS-7 exception).
export default [
  {
    ignores: [
      'atlas/vendor/**',
      '**/node_modules/**',
      '**/build/**',
      'firmware/managed_components/**',
      'daemon/integrations/pi/**',
    ],
  },
  { files: ['**/*.js', '**/*.mjs'], ...js.configs.recommended },
  { files: ['**/*.js', '**/*.mjs'], ...jsdoc.configs['flat/recommended-typescript-flavor-error'] },
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
      'no-restricted-syntax': [
        'error',
        {
          selector: 'ExportDefaultDeclaration',
          message: 'Use a named export (docs/conventions/js.md JS-7).',
        },
      ],
      'jsdoc/require-jsdoc': ['error', { publicOnly: true }],
    },
  },
  { files: ['atlas/js/**'], languageOptions: { globals: globals.browser } },
  {
    files: [
      'atlas/test/**',
      'atlas/tools/**',
      'tools/**',
      'firmware/tools/**',
      'eslint.config.mjs',
    ],
    languageOptions: { globals: globals.node },
  },
  { files: ['eslint.config.mjs'], rules: { 'no-restricted-syntax': 'off' } },
];
