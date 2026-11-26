"""Which finishes buzz, end to end (PROTOCOL.md §6j, issue 20261126-27b7).

The real `agentlink-hook` and `agentlink`, a scratch agentlinkd and two fake devices on
WebSocket, one speaking proto 0.25 and one 0.24.

Checks: with no setting the 0.25 welcome says done_buzz all (cfg key 5 = 1) and the 0.24
welcome has no key 5; `agentlink notify armed` writes ~/.agentlink/notify and the next finish
is preceded by a cfg with only key 5 = 0 on the 0.25 device, nothing on the 0.24 one; a finish
with the setting unchanged sends no cfg; `agentlink notify all` brings key 5 = 1 back;
`agentlink status` and `agentlink notify --json` say the setting; a new link's welcome carries
the setting of the moment. The finish delta carries `dur`, which the watch measures the 30 s
minimum from. It starts its own daemon on another port with a scratch AGENTLINK_HOME; the
user's daemon is not touched. Scratch directories are left in /tmp (printed at the end).

    cd daemon && cargo build --workspace
    uv run --with cbor2 --with websockets python daemon/tools/notify_e2e.py "$PWD/.."

Takes about ten seconds. Needs a Unix.
"""

import asyncio
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from typing import Any

import cbor2  # type: ignore[import-not-found]
import websockets  # type: ignore[import-not-found]

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="aln", dir="/tmp")  # short: a unix socket path is at most 108 bytes
CLAUDE = tempfile.mkdtemp(prefix="alnc", dir="/tmp")
PROJECT = tempfile.mkdtemp(prefix="alnp", dir="/tmp")
PORT = 7894
env = dict(
    os.environ,
    AGENTLINK_HOME=HOME,
    AGENTLINK_USB="0",
    AGENTLINK_BLE="0",
    AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
    CLAUDE_CONFIG_DIR=CLAUDE,
    PATH=f"{BIN}:{os.environ['PATH']}",
)
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE", "AGENTLINK_REMOTE", "AGENTLINK_NOTIFY"):
        del env[k]
with open(f"{REPO}/protocol/vectors/hello_sim.json") as f:
    hello_vec = bytes.fromhex(json.load(f)["cbor_hex"])
Msg = dict[int, Any]

MT_WELCOME, MT_SNAPSHOT, MT_SESSION, MT_CFG = 2, 3, 4, 11
ST_DONE = 4
KEY_DONE_BUZZ, ARMED, ALL = 5, 0, 1

ok = True


def check(name: str, cond: object) -> None:
    """Prints one result and records a failure."""
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)


def hook(name: str, **extra: str) -> None:
    """Runs one Claude Code hook event through agentlink-hook."""
    p = {"session_id": "e2e", "cwd": PROJECT, "hook_event_name": name}
    p.update(extra)
    subprocess.run(
        [f"{BIN}/agentlink-hook", "claude", name],
        env=env,
        cwd=PROJECT,
        timeout=10,
        input=json.dumps(p),
        text=True,
        check=False,
    )


def cli(*args: str) -> tuple[int, str]:
    """Runs the agentlink CLI; returns its exit code and output."""
    r = subprocess.run(
        [f"{BIN}/agentlink", *args],
        env=env,
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
    )
    return r.returncode, r.stdout


class Dev:
    """A fake device on one WebSocket link."""

    def __init__(self, ws: Any, minor: int) -> None:
        self.ws, self.minor = ws, minor

    async def hello(self) -> Msg:
        """Says hello at this minor; returns the welcome body."""
        h = cbor2.loads(hello_vec)
        h[3][0] = [0, self.minor]
        await self.ws.send(cbor2.dumps(h))
        w = await self.rx(lambda m: m.get(0) == MT_WELCOME, 3)
        await self.rx(lambda m: m.get(0) == MT_SNAPSHOT, 3)
        body: Msg = w[3] if w else {}
        return body

    async def rx(self, want: Callable[[Msg], bool], timeout: float) -> Msg | None:
        """The first message `want` accepts, or None."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = cbor2.loads(await asyncio.wait_for(self.ws.recv(), end - time.time()))
            except TimeoutError:
                break
            if want(m):
                got: Msg = m
                return got
        return None

    async def upto_done(self, timeout: float = 5) -> list[Msg]:
        """The messages up to and including the delta that enters done."""
        seen: list[Msg] = []
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = cbor2.loads(await asyncio.wait_for(self.ws.recv(), end - time.time()))
            except TimeoutError:
                break
            seen.append(m)
            if m.get(0) == MT_SESSION and m[3].get(2) == ST_DONE:
                break
        return seen


def read(path: str) -> str:
    """The file's text."""
    with open(path) as f:
        return f.read()


def cfgs(msgs: list[Msg]) -> list[Msg]:
    """The cfg bodies among `msgs`."""
    return [m[3] for m in msgs if m.get(0) == MT_CFG]


def done_delta(msgs: list[Msg]) -> Msg | None:
    """The delta that enters done among `msgs`, or None."""
    return next((m[3] for m in msgs if m.get(0) == MT_SESSION and m[3].get(2) == ST_DONE), None)


async def turn(new: Dev, old: Dev, prompt: str) -> tuple[list[Msg], list[Msg]]:
    """One prompt and stop; what each device got up to done."""
    hook("UserPromptSubmit", prompt=prompt)
    hook("Stop", last_assistant_message="Done.")
    return await new.upto_done(), await old.upto_done()


log = open(f"{HOME}/daemon.log", "w")  # noqa: SIM115  (the daemon writes it until the end)
daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=log, stderr=subprocess.STDOUT)
time.sleep(1.0)


async def main() -> None:
    """The checks, against both devices."""
    async with (
        websockets.connect(f"ws://127.0.0.1:{PORT}/") as w25,
        websockets.connect(f"ws://127.0.0.1:{PORT}/") as w24,
    ):
        new, old = Dev(w25, 25), Dev(w24, 24)
        wn, wo = await new.hello(), await old.hello()
        check(
            f"no setting: the 0.25 welcome says all ({wn.get(3)})",
            wn.get(3, {}).get(KEY_DONE_BUZZ) == ALL,
        )
        check(f"the 0.24 welcome has no key 5 ({wo.get(3)})", KEY_DONE_BUZZ not in wo.get(3, {}))
        hook("SessionStart")

        n, o = await turn(new, old, "fix the build")
        check(f"a finish with the setting unchanged sends no cfg ({cfgs(n)})", cfgs(n) == [])
        d = done_delta(n)
        check(f"the done delta carries dur ({d})", d is not None and 34 in d)

        rc, out = cli("notify", "armed")
        check(
            f"agentlink notify armed: {out.strip()!r}", rc == 0 and out.startswith("notify: armed")
        )
        check(
            "... it writes ~/.agentlink/notify",
            read(f"{HOME}/notify").strip() == "armed",
        )
        n, o = await turn(new, old, "again")
        c = cfgs(n)
        check(
            f"the next finish is preceded by cfg {{5: 0}} on 0.25 ({c})",
            c == [{KEY_DONE_BUZZ: ARMED}],
        )
        check("... before the done delta", n and n[-1].get(0) == MT_SESSION and done_delta(n))
        check(f"... and the 0.24 device gets no cfg ({cfgs(o)})", cfgs(o) == [] and done_delta(o))
        n, _ = await turn(new, old, "once more")
        check(f"unchanged since: no cfg ({cfgs(n)})", cfgs(n) == [])

        rc, out = cli("notify", "--json")
        j: dict[str, Any] = json.loads(out) if rc == 0 else {}
        check(f"agentlink notify --json says armed ({j})", j.get("notify") == "armed")
        _, st = cli("status")
        check("agentlink status prints the setting", "notify: armed" in st)
        rc, _ = cli("notify", "loud")
        check("agentlink notify refuses an unknown setting", rc != 0)

        async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w2:
            w = await Dev(w2, 25).hello()
            check(
                f"a new link's welcome carries armed ({w.get(3)})",
                w.get(3, {}).get(KEY_DONE_BUZZ) == ARMED,
            )

        cli("notify", "all")
        n, o = await turn(new, old, "last")
        check(
            f"notify all: cfg {{5: 1}} before the next finish ({cfgs(n)})",
            cfgs(n) == [{KEY_DONE_BUZZ: ALL}],
        )
        check("... still nothing for 0.24", cfgs(o) == [])


asyncio.run(main())
daemon.send_signal(signal.SIGTERM)
try:
    daemon.wait(8)
except subprocess.TimeoutExpired:
    daemon.kill()
    daemon.wait()
log.close()
print("scratch:", HOME, CLAUDE, PROJECT)
sys.exit(0 if ok else 1)
