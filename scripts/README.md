# scripts

Repo-wide entry points, run from the repository root.

| Path | What it does |
|---|---|
| `check` | The one entry point for checks: `scripts/check [AREA...] [--changed] [--list] [--fail-fast]`; areas `sim daemon protocol atlas firmware docs` ([conventions](../docs/conventions/README.md)) |
| `check.d/` | The scripts `check` runs beside the build tools (formatting, links, commit messages, structure) |
| `hooks/` | Git hooks: `commit-msg` checks the message ([changes.md](../docs/conventions/changes.md) §5); install once with `git config core.hooksPath scripts/hooks` |
| `issue` | The shared work log: `new`, `list`, `show`, `claim`, `log`, `close`, `reopen`, `env` ([issues/README.md](../issues/README.md)) |
| `atlas` | Serve The Atlas: `scripts/atlas [port] [--open]` (default port 8700) |
| `fetch_docs.py` | Re-download the vendor docs under `docs/`: `uv run scripts/fetch_docs.py` |

## Test

```sh
scripts/check --list      # the exact command behind each area
scripts/check --changed   # the areas your branch touches
```

## Rules

- S-NEW-06: a script run from the root goes here; a helper for one component goes in its `tools/`.
- S-TOOL-01: a Python script is one file, run with `uv run` (PEP 723) or `python3` (stdlib only); it
  never imports another component's Python. Follow [python.md](../docs/conventions/python.md).
- Shell scripts run under bash 3.2 too (the macOS system bash), as `check` does.
