# Agent guide

Several agents work on this repository at once, on different machines. Before you start, read
[`issues/README.md`](issues/README.md). It explains how to file, claim, log and close work with
`scripts/issue`, and which checks each area needs. In short:

```sh
export AGENT_NAME=<you> AGENT_HOST=<machine label>   # no hostnames or usernames: this repo is public
git pull --rebase && scripts/issue list
scripts/issue claim ID        # before you edit anything
scripts/issue log ID "…"      # as you go
scripts/issue close ID fixed "commit …; checks …"
```

The design docs are under `docs/design/` and the repository map is in `README.md`.
