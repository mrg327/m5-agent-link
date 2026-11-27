# bench

Task scripts that the bench plays on the real watch, as a finger, to measure what the user feels:
task time, how late a touch is read, touch-to-frame latency and frame times. The same scripts run in
the simulator as tests. Design and first results: [BENCH.md](../docs/design/BENCH.md).

## Run and test

```sh
agentlink bench run bench/tasks/*.txt --runs 3 [--human 1]   # on the watch, through the daemon
ctest --test-dir build/sim -R sim_task_ --output-on-failure  # in the simulator (part of scripts/check sim)
```

The watch needs firmware with proto 0.24, and the daemon that runs the bench must hold its link. To
try a daemon build beside the service, see BENCH.md §7 and [AGENTS.md](AGENTS.md).

| Task | What it does |
|---|---|
| `approve.txt`, `deny.txt` | A permission card arrives; hold the primary disc to approve, or step to Deny with B and hold |
| `answer.txt` | A question with three options; tap the second |
| `reply.txt` | A closing question; open it, go to Reply and send the first answer |
| `switch.txt` | Two agents; drag along the rim to the other and bring its terminal forward |
| `agent_back.txt`, `watch.txt`, `status.txt`, `labs.txt`, `busy.txt`, `arrival.txt` | Screen tasks from the UX profile ([UX-PROFILE.md](../docs/design/UX-PROFILE.md)); their `expect_screen` lines are the check |
| `mote_visit.txt`, `mote_hover.txt`, `mote_empty.txt`, `mote_alert.txt` | The Mote's frame cost ([BENCH.md](../docs/design/BENCH.md) §9): the idle face's linger and trip, the hover at an error, the empty deck, a request during a trip. Switch the Mote on first (LABS > Personality, the dot) and run on USB |

The script language is the simulator's ([simulator/README.md](../simulator/README.md)): `touch` and
`bench start|stop|mark|save` are the bench's own commands. Each script begins with `connect` and a
proto 0.24 welcome. A new task goes in `tasks/<name>.txt` and gets a ctest line in
`simulator/CMakeLists.txt` (S-NEW-06).
