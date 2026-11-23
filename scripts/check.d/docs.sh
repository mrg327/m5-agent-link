#!/usr/bin/env bash
# scripts/check.d/docs.sh: the `docs` area of scripts/check (docs/conventions/docs.md §7).
#
#   1. relative links and #anchors resolve      python3 scripts/check.d/links.py   (DOC-26)
#   2. spelling                                 typos, config _typos.toml
#   3. Markdown style                           markdownlint-cli2, config .markdownlint-cli2.jsonc
#                                               (DOC-23, DOC-24, DOC-27)
#
# Tool versions are pinned here so CI and humans run the same thing. A missing tool is a failure,
# not a skip: CI must be deterministic. Runs every step, then exits 1 if any failed.

set -euo pipefail

cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

TYPOS_VERSION=1.52.0
MARKDOWNLINT_CLI2_VERSION=0.24.0

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "docs: '$1' not found. $2" >&2
        exit 1
    fi
}
need python3 "Install Python 3.11 or newer."
need uvx "Install uv: https://docs.astral.sh/uv/getting-started/installation/"
need npx "Install Node.js 20 or newer: https://nodejs.org/"

failed=0
run() {
    echo "+ $*"
    if ! "$@"; then
        failed=1
    fi
}

run python3 scripts/check.d/links.py
run uvx "typos@${TYPOS_VERSION}" --format brief
run npx --yes "markdownlint-cli2@${MARKDOWNLINT_CLI2_VERSION}" '**/*.md'

if [ "$failed" -ne 0 ]; then
    echo "docs: FAILED. Fix the lines above; docs/conventions/docs.md says how." >&2
    exit 1
fi
echo "docs: ok"
