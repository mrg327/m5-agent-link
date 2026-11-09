# Issues: the shared work log for humans and agents

Several agents (Claude Code, Copilot CLI and their sub-agents) work on this repository, often at
the same time and on different machines: a Linux dev box, a macOS work laptop, Windows for the
daemon. This directory is where they file, claim, log and close work so nobody duplicates or
clobbers it. Each entry records the machine it was written on, so a bug that only shows up on
one platform can be told apart.

The tracker is git itself, with one Markdown file per issue. It works offline, needs no
account, and merges cleanly because two agents never edit the same file unless they work the
same issue. GitHub Issues is an optional mirror (`scripts/issue gh ID`) for machines whose `gh`
can write to the repo. **The file in git is the source of truth.**

## Identify yourself

Export these two variables before using the tool. They appear in every log line.

```sh
export AGENT_NAME=claude-code          # or copilot-cli/opus, copilot-cli/sonnet-fw, matt …
export AGENT_HOST=linux-dev            # a short label for the machine: mac-work, linux-dev, win-ci …
```

**Privacy rule: this repository is public.** Do not put hostnames, usernames, IP addresses,
home-directory paths, tokens or internal URLs in issues. The tool records only the OS, CPU
architecture, git revision and toolchain versions, plus the labels above.

## The tool

```sh
scripts/issue new -t "Waking tap leaks into the UI" -s high -a firmware -p any -b - <<'EOF'
What happens, how to reproduce it (file:line, simulator script, command), what should happen.
EOF
scripts/issue list                 # open and claimed issues (also: list fixed, list all)
scripts/issue show ID
scripts/issue claim ID             # sets owner and status=claimed; refuses if someone else owns it
scripts/issue log ID "message"     # progress, findings, hand-off notes
scripts/issue close ID fixed "commit abc1234; ctest 45/45"   # or wontfix / duplicate
scripts/issue reopen ID "still happens on linux-x86_64"
scripts/issue env                  # print the environment stamp
```

You may use a unique prefix of an ID.

| Field | Values |
|---|---|
| `status` | `open` → `claimed` → `fixed` / `wontfix` / `duplicate` (and `reopen`) |
| `severity` | `high` (wrong action, data loss, security), `med` (wrong state, CI red), `low` |
| `area` | `firmware daemon protocol simulator atlas board docs ci tools` |
| `platforms` | where it reproduces: `any`, or a list such as `macos`, `linux`, `windows`, `esp32` |
| `reported_on` / `verified_on` | platforms recorded automatically when the issue is filed and when it is fixed |

## Protocol

1. **Look before you start.** Run `git pull --rebase` and then `scripts/issue list`. If an
   issue already covers your problem, log on it instead of filing a duplicate.
2. **Claim before you edit code.** Run `scripts/issue claim ID`, commit only that file
   (`issue: claim ID`) and push straight away. The push acts as the lock. If it is rejected,
   pull, run `scripts/issue show ID` and back off if someone else claimed it first. Sub-agents
   of one orchestrator may skip the push: the orchestrator assigns issues to them directly and
   gives each one strictly separate files to edit.
3. **One issue, one owner, and one set of files.** If your fix has to touch files covered by
   another claimed issue, log on that issue and coordinate before you edit them.
4. **Log as you go.** Write down what you found, what you tried that failed, and anything
   platform-specific ("passes on macos-arm64, ctest sim_status times out on linux-x86_64").
   Each log line is stamped with the agent, host label, platform and git revision.
5. **Close with evidence.** Close in the same commit as the fix, or right after it. The close
   message names the commit and the checks that passed. Closing records the full environment
   and adds your platform to `verified_on`. A fix that is verified on only one platform, for a
   platform-sensitive area (power, BLE/USB, focus drivers, build), should say so. Leave
   `platforms` honest so another machine can confirm it.
6. **Found something out of scope?** File a new issue instead of widening your change.
7. **Commit messages** for fixes reference the issue ID, e.g. `power: retry a refused deep
   sleep (issue 20261109-a1b2)`.

## Checks to run before closing

| Area | Command |
|---|---|
| firmware / simulator | `cmake -S simulator -B build/sim -G Ninja && cmake --build build/sim && ctest --test-dir build/sim --output-on-failure` |
| firmware (device) | `. ~/esp/esp-idf/export.sh && idf.py -C firmware build` |
| daemon | `cd daemon && cargo fmt --check && cargo clippy --workspace -- -D warnings && cargo test --workspace` |
| protocol | `uv run protocol/tools/validate_vectors.py && uv run protocol/tools/gen_vectors.py && git diff --exit-code protocol/vectors` |
| atlas | `python3 atlas/tools/extract.py --check && node --test atlas/test/*.test.mjs` (after any firmware edit, run `python3 atlas/tools/extract.py` and commit `facts.json`) |
