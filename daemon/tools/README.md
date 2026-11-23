# daemon/tools

End-to-end tests that run the real `agentlink-hook`, `agentlinkd` and `agentlink` against fake devices
on WebSocket, usually one at a feature's proto minor and one below it, plus the macOS launchd helper.
Each test starts its own daemon on another port with a scratch `AGENTLINK_HOME`; the user's daemon is
not touched.

## Run

```sh
cd daemon && cargo build --workspace && cd ..
uv run --with cbor2 --with websockets python daemon/tools/<name>_e2e.py "$PWD"   # from the repo root
```

`ota_e2e.py` also needs `--with detools==0.53.1`; `power_e2e.py` needs only `python3`. The exact line
is in the comment at the top of each script.

| Script | Feature (design doc) |
|---|---|
| `answer_window_e2e.py` | Answer window and full text ([ANSWER-WINDOW.md](../../docs/design/ANSWER-WINDOW.md)) |
| `gauges_e2e.py` | Status line to gauges ([GAUGES.md](../../docs/design/GAUGES.md)) |
| `jobs_e2e.py` | Terminal jobs in a real bash pty ([JOBS.md](../../docs/design/JOBS.md)) |
| `last_message_e2e.py` | Last message and questions ([LAST-MESSAGE.md](../../docs/design/LAST-MESSAGE.md)) |
| `ota_e2e.py` | Firmware update ([FIRMWARE.md](../../docs/design/FIRMWARE.md) §9) |
| `power_e2e.py` | Power report log and `agentlink power` ([POWER.md](../../docs/design/POWER.md)) |
| `presence_e2e.py` | Presence and keep-awake ([PRESENCE.md](../../docs/design/PRESENCE.md)) |
| `standing_rules_e2e.py` | Standing rules ([STANDING-RULES.md](../../docs/design/STANDING-RULES.md)) |
| `summary_e2e.py` | Done summary ([DONE-SUMMARY.md](../../docs/design/DONE-SUMMARY.md)) |
| `launchd.sh` | macOS: `cert`, `install`, `status`, `uninstall` the launchd agent |

A new feature adds `<feature>_e2e.py` here (S-NEW-04); follow [python.md](../../docs/conventions/python.md)
(PY-23).
