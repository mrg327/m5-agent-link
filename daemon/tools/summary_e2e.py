# Done summary end to end (docs/design/DONE-SUMMARY.md): fake Claude Code hook calls for a whole turn (prompt, edits,
# a failing then a passing test command, stop) through `agentlink-hook` -> agentlinkd -> fake devices on WebSocket,
# one speaking proto 0.14 and one 0.13. Checks the fields the 0.14 device receives with the delta that enters done
# (files, lines, tests, seconds), the reset at the next prompt, that unknown stays absent, that the 0.13 device sees
# none of keys 30-34 and that no file name reaches either device. It starts its own daemon on another port with a
# scratch AGENTLINK_HOME; the user's daemon is not touched. Scratch directories are left in /tmp (printed at the end).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/summary_e2e.py "$PWD/.."     # the repo root
#
# Takes about ten seconds. Needs a Unix.
import asyncio, json, os, subprocess, sys, tempfile, time, signal
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="als", dir="/tmp")  # short: a unix socket path is at most 108 bytes
PORT = 7896
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_KEEP_AWAKE="off",
           AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}", PATH=f"{BIN}:{os.environ['PATH']}")
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE", "AGENTLINK_REMOTE"): del env[k]
hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])
SID, SECRET = "e2e-turn", "secret-file-name-xyz"
KEYS = (30, 31, 32, 33, 34)

def hook(event, **payload):
    payload.update(session_id=SID, cwd=REPO, permission_mode="default", hook_event_name=event)
    p = subprocess.run([f"{BIN}/agentlink-hook", "claude", event], input=json.dumps(payload), capture_output=True,
                       text=True, env=env, timeout=10)
    assert p.returncode == 0, p.stderr

def edit(path, old, new):
    hook("PostToolUse", tool_name="Edit", tool_input={"file_path": path, "old_string": old, "new_string": new},
         tool_response={"filePath": path})

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

class Dev:
    def __init__(self, ws, minor):
        self.ws, self.minor, self.got, self.raw = ws, minor, [], b""
    async def hello(self):
        h = cbor2.loads(hello_vec)
        h[3][0] = [0, self.minor]
        await self.ws.send(cbor2.dumps(h))
    async def rx(self, want, timeout):
        end = time.time() + timeout
        while time.time() < end:
            try: raw = await asyncio.wait_for(self.ws.recv(), end - time.time())
            except asyncio.TimeoutError: break
            self.raw += raw
            m = cbor2.loads(raw)
            self.got.append(m)
            if want(m): return m
        return None

def done_delta(sid):
    return lambda m: m.get(0) == 4 and m[3].get(0) == sid and m[3].get(2) in (3, 4)  # error / done

async def main():
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w14, websockets.connect(f"ws://127.0.0.1:{PORT}/") as w13:
        new, old = Dev(w14, 14), Dev(w13, 13)
        for d in (new, old):
            await d.hello()
            await d.rx(lambda m: m.get(0) == 3, 3)  # snapshot
        hook("SessionStart")
        s = await new.rx(lambda m: m.get(0) == 4, 3)
        sid = s[3][0] if s else 0
        # turn 1: two edits of one file, a write of a new file, a failing test, a fixing edit, a passing test
        hook("UserPromptSubmit", prompt="fix the build")
        edit(f"/work/{SECRET}.rs", "let a = 1;", "let a = 2;\nlet b = 3;")
        edit(f"/work/{SECRET}.rs", "x\ny", "x")
        hook("PostToolUse", tool_name="Write", tool_input={"file_path": f"/work/{SECRET}_new.rs", "content": "1\n2\n3\n4\n"},
             tool_response={"type": "create", "filePath": f"/work/{SECRET}_new.rs"})
        hook("PostToolUseFailure", tool_name="Bash", tool_input={"command": "cd daemon && cargo test --workspace"},
             error="Exit code 101\nthread panicked", is_interrupt=False)
        check("a failed test run alone says nothing yet", await new.rx(lambda m: m.get(0) == 4 and any(k in m[3] for k in KEYS), 0.8) is None)
        hook("PostToolUse", tool_name="Bash", tool_input={"command": "cd daemon && cargo test --workspace"},
             tool_response={"stdout": "ok", "stderr": "", "interrupted": False})
        await asyncio.sleep(1.2)  # a measurable turn
        hook("Stop", last_assistant_message="Done.")
        d = await new.rx(done_delta(sid), 3)
        b = d[3] if d else {}
        check("0.14 device: the delta that enters done carries the summary", d and all(k in b for k in (30, 31, 32, 33, 34)))
        check("2 files (a file edited twice counts once)", b.get(30) == 2)
        check("lines: +2 -1, +0 -1, +4 -0 = +6 -2", (b.get(31), b.get(32)) == (6, 2))
        check("tests: the last run passed (true)", b.get(33) is True)
        check("turn time is whole seconds, at least 1", isinstance(b.get(34), int) and 1 <= b[34] <= 20)
        # turn 2: a prompt and a stop with nothing in between: only the time is known, never a zero
        hook("UserPromptSubmit", prompt="thanks")
        hook("Stop", last_assistant_message="You are welcome.")
        d = await new.rx(done_delta(sid), 3)
        b = d[3] if d else {}
        check("a turn without edits or tests: only the time", d and 34 in b and not any(k in b for k in (30, 31, 32, 33)))
        # turn 3: a failing test is the verdict
        hook("UserPromptSubmit", prompt="run the tests")
        hook("PostToolUseFailure", tool_name="Bash", tool_input={"command": "pytest -x"}, error="Exit code 1\nFAILED",
             is_interrupt=False)
        hook("Stop", last_assistant_message="Two tests fail.")
        d = await new.rx(done_delta(sid), 3)
        b = d[3] if d else {}
        check("failed tests: tp false, no files", d and b.get(33) is False and 30 not in b)
        # turn 4: an unrecognised or ambiguous command leaves the tests out
        hook("UserPromptSubmit", prompt="again")
        hook("PostToolUse", tool_name="Bash", tool_input={"command": "cargo test | tail -3"}, tool_response={"stdout": "", "interrupted": False})
        hook("Stop", last_assistant_message="Done.")
        d = await new.rx(done_delta(sid), 3)
        check("a piped test command has no verdict", d and 33 not in d[3])
        # the 0.13 device: states, no summary keys
        await old.rx(lambda m: False, 1)
        deltas = [m for m in old.got if m.get(0) == 4]
        check("0.13 device saw the session end turns", sum(1 for m in deltas if m[3].get(2) in (3, 4)) >= 3)
        check("0.13 device: none of keys 30-34", not any(k in m[3] for m in deltas for k in KEYS))
        check("no file name reached either device", SECRET.encode() not in new.raw and SECRET.encode() not in old.raw)
        # a device connecting after the turn gets the last summary with the snapshot
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w:
        late = Dev(w, 14)
        await late.hello()
        snap = await late.rx(lambda m: m.get(0) == 3, 3)
        ss = [x for x in snap[3][0]] if snap else []
        mine = [x for x in ss if x.get(15) is not None or x.get(2) in (3, 4)]
        check("a device connecting later gets the stored summary in the snapshot",
              any(34 in x for x in mine) and not any(33 in x for x in mine))
    return True

asyncio.run(main())
daemon.send_signal(signal.SIGTERM); daemon.wait(5)
print("scratch:", HOME)
sys.exit(0 if ok else 1)
