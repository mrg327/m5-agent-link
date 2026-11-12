# Gauges end to end (docs/design/GAUGES.md): a Claude Code status-line JSON piped into `agentlink-hook statusline`
# -> agentlinkd -> fake devices on WebSocket, one speaking proto 0.10 and one 0.9. Checks the chained status line
# (its output and exit status pass through, quickly, with or without a daemon), the session readouts and the
# usage message on the 0.10 device, nothing new on the 0.9 one, and that paths and costs never reach the
# daemon's files. It starts its own daemon on another port with a scratch AGENTLINK_HOME; the user's daemon is
# not touched. Scratch directories are left in /tmp (printed at the end).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/gauges_e2e.py "$PWD/.."     # the repo root
#
# Takes about ten seconds. Needs a Unix.
import asyncio, json, os, subprocess, sys, tempfile, time, signal
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="alg", dir="/tmp")  # short: a unix socket path is at most 108 bytes
NODAEMON = tempfile.mkdtemp(prefix="alg0", dir="/tmp")
PORT = 7898
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
           PATH=f"{BIN}:{os.environ['PATH']}")
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE", "AGENTLINK_REMOTE"): del env[k]
hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])
NOW = int(time.time())

def statusline(sid, ctx=72, h5=42.5, wk=18.0, pr=128):
    return json.dumps({
        "cwd": REPO, "session_id": sid, "transcript_path": "/secret-transcript/t.jsonl",
        "model": {"id": "claude-opus-4-7", "display_name": "Opus 4.7 (1M context)"},
        "workspace": {"current_dir": REPO, "project_dir": REPO}, "version": "2.1.300",
        "cost": {"total_cost_usd": 12.3456, "total_duration_ms": 45000},
        "context_window": {"used_percentage": ctx, "context_window_size": 200000},
        "rate_limits": {"five_hour": {"used_percentage": h5, "resets_at": NOW + 7800},
                        "seven_day": {"used_percentage": wk, "resets_at": NOW + 280000}},
        "pr": {"number": pr, "url": "https://example.invalid/pull/128", "review_state": "approved"},
    })

def shim(args, stdin, home=HOME):
    t = time.time()
    p = subprocess.run([f"{BIN}/agentlink-hook", *args], input=stdin, capture_output=True, text=True,
                       env=dict(env, AGENTLINK_HOME=home), timeout=10)
    return p, time.time() - t

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

class Dev:
    def __init__(self, ws, minor):
        self.ws, self.minor, self.got = ws, minor, []
    async def hello(self):
        h = cbor2.loads(hello_vec)
        h[3][0] = [0, self.minor]
        await self.ws.send(cbor2.dumps(h))
    async def rx(self, want, timeout):
        end = time.time() + timeout
        while time.time() < end:
            try: m = cbor2.loads(await asyncio.wait_for(self.ws.recv(), end - time.time()))
            except asyncio.TimeoutError: break
            self.got.append(m)
            if want(m): return m
        return None

async def main():
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w10, websockets.connect(f"ws://127.0.0.1:{PORT}/") as w9:
        new, old = Dev(w10, 10), Dev(w9, 9)
        for d in (new, old):
            await d.hello()
            await d.rx(lambda m: m.get(0) == 3, 3)  # snapshot
        # a status line before any hook: no card, but the account's figures count
        p, dt = shim(["statusline"], statusline("e2e-early"))
        check("statusline without a chain prints nothing and exits 0", p.returncode == 0 and p.stdout == "")
        check(f"statusline returns quickly ({dt * 1000:.0f} ms)", dt < 0.5)
        u = await new.rx(lambda m: m.get(0) == 23, 3)
        check("0.10 device gets usage: 5h 42 %, week 18 %, reset times", u and u[3].get(1) == 42 and u[3].get(2) == NOW + 7800
              and u[3].get(4) == 18 and u[3].get(5) == NOW + 280000)
        check("the reading carries its time", u and abs(u[3].get(0, 0) - time.time()) < 5)
        check("no session was made by a status line", not any(m.get(0) == 4 for m in new.got))
        # the hooks make the session; the permission mode rides on them
        hook = json.dumps({"session_id": "e2e", "cwd": REPO, "permission_mode": "plan", "hook_event_name": "SessionStart"})
        shim(["claude", "SessionStart"], hook)
        s = await new.rx(lambda m: m.get(0) == 4, 3)
        check("session created by the hook carries pm plan (1)", s and s[3].get(23) == 1)
        sid = s[3][0] if s else 0
        s9 = await old.rx(lambda m: m.get(0) == 4, 3)
        check("0.9 device gets the session without keys 21-25", s9 and not any(k in s9[3] for k in (21, 22, 23, 24, 25)))
        # the status line of that session: readouts as one delta
        p, dt = shim(["statusline", "--", "cat > /dev/null; echo 'my own line'; exit 3"], statusline("e2e"))
        check("the chained command's output passes through", p.stdout == "my own line\n")
        check("and its exit status", p.returncode == 3)
        check(f"chained run returns quickly ({dt * 1000:.0f} ms)", dt < 0.5)
        d = await new.rx(lambda m: m.get(0) == 4 and m[3].get(0) == sid and 21 in m[3], 3)
        check("readouts: ctx 72, model Opus 4.7, PR 128 approved", d and d[3].get(21) == 72 and d[3].get(22) == "Opus 4.7"
              and d[3].get(24) == 128 and d[3].get(25) == 2)
        # chaining sees the full input
        p, _ = shim(["statusline", "--", "python3 -c 'import json,sys; print(json.load(sys.stdin)[\"cost\"][\"total_cost_usd\"])'"],
                    statusline("e2e"))
        check("the chained command gets the whole JSON", p.stdout.strip() == "12.3456")
        # after /compact the context is null: cleared with 255
        line = json.loads(statusline("e2e")); line["context_window"]["used_percentage"] = None
        shim(["statusline"], json.dumps(line))
        d = await new.rx(lambda m: m.get(0) == 4 and m[3].get(21) == 255, 3)
        check("a null context clears the figure (ctx 255)", d is not None)
        # the 5-hour window rises: a new usage body
        shim(["statusline"], statusline("e2e", h5=81.2))
        u = await new.rx(lambda m: m.get(0) == 23, 3)
        check("usage follows the figure: 81 %", u and u[3].get(1) == 81)
        # a session without figures (first response not yet in) keeps the account's
        line = json.loads(statusline("e2e")); del line["rate_limits"]
        shim(["statusline"], json.dumps(line))
        u = await new.rx(lambda m: m.get(0) == 23, 1.5)
        check("a line without rate_limits sends no usage", u is None)
        # the old device saw none of it
        await old.rx(lambda m: False, 1)
        check("0.9 device: no usage message", not any(m.get(0) == 23 for m in old.got))
        check("0.9 device: no gauge keys in any delta", not any(m.get(0) == 4 and any(k in m[3] for k in (21, 22, 23, 24, 25))
                                                              for m in old.got))
        # garbage in: nothing breaks, the chain still runs
        p, _ = shim(["statusline", "--", "echo fine"], "not json")
        check("bad input: the chain still runs", p.returncode == 0 and p.stdout == "fine\n")
    return True

asyncio.run(main())
# no daemon at all: silent, fast, the chain still works
p, dt = shim(["statusline", "--", "echo alone"], statusline("e2e"), home=NODAEMON)
check(f"no daemon: the chain's output, exit 0, quickly ({dt * 1000:.0f} ms)", p.stdout == "alone\n" and p.returncode == 0 and dt < 0.5)
daemon.send_signal(signal.SIGTERM); daemon.wait(5)
blob = ""
for root, _, files in os.walk(HOME):
    for f in files:
        try: blob += open(os.path.join(root, f), errors="replace").read()
        except Exception: pass
leak = [w for w in ("secret-transcript", "12.3456", "example.invalid") if w in blob]
check(f"no transcript path, cost or PR url under AGENTLINK_HOME: {leak}", not leak)
print("scratch:", HOME, NODAEMON)
sys.exit(0 if ok else 1)
