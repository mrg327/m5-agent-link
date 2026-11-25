# Trusted watches end to end (PROTOCOL.md §8, agentlinkd trust.rs).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/trust_e2e.py "$PWD/.."
"""Trusted watches end to end: a scratch agentlinkd and fake watches on WebSocket.

The daemon judges WebSocket links like BLE ones (AGENTLINK_TRUST_WS_AS_BLE, test only). Checks: the
first watch is recorded and served (welcome); a second watch is refused (no welcome, no snapshot,
listed with trusted false in `agentlink status`); `agentlink devices trust` lets it in on its next
connection; `forget` shuts it out again. The user's own daemon is left alone.
"""

from __future__ import annotations

import asyncio
import json
import os
import subprocess
import sys
import tempfile
import time
from typing import Any

import cbor2
import websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="alt", dir="/tmp")  # short: a unix socket path is at most 108 bytes
PORT = 7911
ENV = dict(
    os.environ,
    AGENTLINK_HOME=HOME,
    AGENTLINK_USB="0",
    AGENTLINK_BLE="0",
    AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
    AGENTLINK_TRUST_WS_AS_BLE="1",
)
with open(f"{REPO}/protocol/vectors/hello_sim.json", encoding="utf-8") as vec:
    BASE: Any = cbor2.loads(bytes.fromhex(json.load(vec)["cbor_hex"]))

results: list[bool] = []


def hello(uid_hex: str) -> Any:
    """Returns the simulator's hello vector with its device uid replaced.

    Args:
        uid_hex: The device uid, as hex.
    """
    h = cbor2.loads(cbor2.dumps(BASE))
    h[3][1][3] = bytes.fromhex(uid_hex)
    return h


def cli(*args: str) -> str:
    """Runs the agentlink CLI against the scratch home and returns its stdout and stderr.

    Args:
        *args: The CLI arguments.
    """
    r = subprocess.run(
        [f"{BIN}/agentlink", *args], env=ENV, capture_output=True, text=True, check=False
    )
    return r.stdout + r.stderr


def check(name: str, cond: object) -> None:
    """Prints one check's result and keeps it for the exit code.

    Args:
        name: What is checked.
        cond: Truthy when it passed.
    """
    print(("PASS " if cond else "FAIL ") + name)
    results.append(bool(cond))


async def connect(uid_hex: str, wait: float = 2.0) -> tuple[bool, list[Any], Any]:
    """Connects a fake watch and sends its hello.

    Args:
        uid_hex: The watch's device uid, as hex.
        wait: Seconds to collect what the daemon sends.

    Returns:
        Whether a welcome came, the message types seen, and the open socket.
    """
    ws = await websockets.connect(f"ws://127.0.0.1:{PORT}/")
    await ws.send(cbor2.dumps(hello(uid_hex)))
    seen: list[Any] = []
    end = time.time() + wait
    while time.time() < end:
        try:
            raw = await asyncio.wait_for(ws.recv(), end - time.time())
        except (TimeoutError, websockets.ConnectionClosed):
            break
        m = cbor2.loads(raw if isinstance(raw, bytes) else raw.encode())
        seen.append(m.get(0))
    return 2 in seen, seen, ws


def status_devices() -> list[Any]:
    """Returns the devices `agentlink status` lists."""
    out = cli("--json", "status")
    try:
        devices: list[Any] = json.loads(out).get("devices", [])
    except json.JSONDecodeError:
        return []
    return devices


async def scenario() -> None:
    """Runs the checks against the running scratch daemon."""
    a, b = "a1a2a3a4a5a6a7a8", "b1b2b3b4b5b6b7b8"
    welcomed, _, ws_a = await connect(a)
    check("first watch is served (welcome)", welcomed)
    check("first watch is recorded", a in cli("devices"))
    welcomed, seen, ws_b = await connect(b)
    check("second watch gets no welcome", not welcomed)
    check("second watch sees no snapshot or session", not ({3, 4, 6} & set(seen)))
    untrusted = [d for d in status_devices() if d.get("trusted") is False]
    check("status lists one untrusted watch", len(untrusted) == 1)
    await ws_b.close()
    check("trust it", "trusted" in cli("devices", "trust", b))
    welcomed, _, ws_b = await connect(b)
    check("trusted watch is served on its next connection", welcomed)
    await ws_b.close()
    cli("devices", "forget", b)
    welcomed, _, ws_b = await connect(b)
    check("forgotten watch is refused again", not welcomed)
    await ws_b.close()
    await ws_a.close()


def main() -> int:
    """Starts the scratch daemon, runs the scenario and stops the daemon.

    Returns:
        The exit code: 0 when every check passed.
    """
    with open(f"{HOME}/daemon.log", "w", encoding="utf-8") as log:
        daemon = subprocess.Popen(
            [f"{BIN}/agentlinkd"], env=ENV, stdout=log, stderr=subprocess.STDOUT
        )
        try:
            time.sleep(1.0)
            asyncio.run(scenario())
        finally:
            daemon.terminate()
            daemon.wait()
    ok = all(results)
    print("OK" if ok else "FAILED", "home:", HOME)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
