# Answer window and full text end to end (docs/design/ANSWER-WINDOW.md): a Claude Code PermissionRequest piped
# into the real `agentlink-hook` -> agentlinkd -> fake devices on WebSocket, one speaking proto 0.11 and one 0.10.
# Checks the window's length and the countdown on the wire, the full text of a long command fetched in chunks by
# rid and approved after it was read, the hand-back when the window runs out and by Open on PC, a window cut to
# fit the hook's own Claude Code timeout, a killed hook, an older hook that names no window, and that the 0.10
# device sees none of the new keys. It starts its own daemon on another port with a scratch AGENTLINK_HOME and a
# scratch Claude Code settings directory; the user's daemon and settings are not touched. Scratch directories are
# left in /tmp (printed at the end).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/answer_window_e2e.py "$PWD/.."     # the repo root
#
# Takes about half a minute. Needs a Unix.
import asyncio, json, os, signal, socket, subprocess, sys, tempfile, threading, time
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="alw", dir="/tmp")  # short: a unix socket path is at most 108 bytes
CLAUDE = tempfile.mkdtemp(prefix="alwc", dir="/tmp")  # the "user's" Claude Code settings, for the hook timeout
PROJECT = tempfile.mkdtemp(prefix="alwp", dir="/tmp")  # the agent's working directory: no project settings
PORT = 7897
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
           CLAUDE_CONFIG_DIR=CLAUDE, PATH=f"{BIN}:{os.environ['PATH']}")
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE", "AGENTLINK_REMOTE", "AGENTLINK_WAIT_MS",
                                      "AGENTLINK_ANSWER_WINDOW_S"):
        del env[k]
hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])

MT_ATT, MT_CLEAR, MT_CMD, MT_RESULT, MT_DETAIL = 6, 7, 8, 9, 10
OP_FOCUS, OP_APPROVE, OP_DETAIL = 0, 3, 7
LONG = " && ".join(f"kubectl --context prod-eu-{i} rollout restart deployment/api-gateway --namespace edge" for i in range(24))
LONG += "\nrm -rf ./build --no-preserve-root"  # a line break and a no-break space: both must stay visible

def settings(timeout):
    hook = {"type": "command", "command": "agentlink-hook claude PermissionRequest"}
    if timeout is not None:
        hook["timeout"] = timeout
    open(f"{CLAUDE}/settings.json", "w").write(json.dumps({"hooks": {"PermissionRequest": [{"hooks": [hook]}]}}))

def window_file(secs):
    path = f"{HOME}/answer_window"
    if secs is None:
        if os.path.exists(path): os.remove(path)
    else:
        open(path, "w").write(f"{secs}\n")

def permission(cmd):
    return json.dumps({"session_id": "e2e", "cwd": PROJECT, "hook_event_name": "PermissionRequest",
                       "permission_mode": "default", "tool_name": "Bash", "tool_input": {"command": cmd},
                       "permission_suggestions": []})

class Hook:
    """agentlink-hook claude PermissionRequest, running until the daemon releases it."""
    def __init__(self, payload):
        self.t0 = time.time()
        self.p = subprocess.Popen([f"{BIN}/agentlink-hook", "claude", "PermissionRequest"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env, cwd=PROJECT)
        self.p.stdin.write(payload)
        self.p.stdin.close()
        self.out, self.took = None, None
        threading.Thread(target=self._wait, daemon=True).start()
    def _wait(self):
        self.out = self.p.stdout.read()
        self.p.wait()
        self.took = time.time() - self.t0
    async def done(self, timeout):
        end = time.time() + timeout
        while self.took is None and time.time() < end:
            await asyncio.sleep(0.05)
        return self.took is not None

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

class Dev:
    def __init__(self, ws, minor):
        self.ws, self.minor, self.got, self.next_id = ws, minor, [], 10
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
    async def cmd(self, op, sid, rid=None, arg=None, hold=None):
        self.next_id += 1
        b = {0: op, 1: sid}
        if rid is not None: b[2] = rid
        if arg is not None: b[3] = arg
        if hold is not None: b[4] = hold
        await self.ws.send(cbor2.dumps({0: MT_CMD, 1: self.next_id, 3: b}))
        return self.next_id

def attention(m): return m.get(0) == MT_ATT
def clear_of(rid): return lambda m: m.get(0) == MT_CLEAR and m[3].get(0) == rid
def result_for(i): return lambda m: m.get(0) == MT_RESULT and m.get(2) == i

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

async def main():
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w11, websockets.connect(f"ws://127.0.0.1:{PORT}/") as w10:
        new, old = Dev(w11, 11), Dev(w10, 10)
        for d in (new, old):
            await d.hello()
            await d.rx(lambda m: m.get(0) == 3, 3)  # snapshot
        subprocess.run([f"{BIN}/agentlink-hook", "claude", "SessionStart"], env=env, cwd=PROJECT, timeout=10,
                       input=json.dumps({"session_id": "e2e", "cwd": PROJECT, "hook_event_name": "SessionStart"}), text=True)
        s = await new.rx(lambda m: m.get(0) == 4, 3)
        sid = s[3][0] if s else 0
        check("the hook made a session", sid > 0)

        # ── 1. default window, countdown on the wire, the full text in chunks, approve after reading ──
        settings(330)
        window_file(None)
        h = Hook(permission(LONG))
        a = await new.rx(attention, 5)
        b = a[3] if a else {}
        rid = b.get(0)
        check(f"default window: win {b.get(9 + 1)} ms = 60 s", b.get(10) == 60000)
        check(f"countdown: {b.get(9)} ms left when sent, at most the window", b.get(9) is not None and 58000 <= b[9] <= 60000)
        check(f"the preview is cut and says so: len {b.get(11)} > 160", b.get(11) is not None and b[11] > 160)
        check("the preview is not the command (cut with an ellipsis)", str(b.get(4, "")).endswith("…"))
        a10 = await old.rx(attention, 3)
        check("the 0.10 device gets the request without keys 9-11", a10 and not any(k in a10[3] for k in (9, 10, 11)))
        i = await new.cmd(OP_DETAIL, sid, rid, 0)
        chunks, r = [], None
        while True:
            m = await new.rx(lambda m: m.get(0) in (MT_DETAIL, MT_RESULT), 5)
            if m is None: break
            if m[0] == MT_DETAIL: chunks.append(m[3])
            else:
                r = m
                break
        full = "".join(c[2] for c in chunks)
        check(f"detail in {len(chunks)} chunks of <= 1024 bytes, in order", len(chunks) >= 2 and
              all(len(c[2].encode()) <= 1024 for c in chunks) and
              [c[3] for c in chunks] == [sum(len(x[2].encode()) for x in chunks[:k]) for k in range(len(chunks))])
        check("every chunk names the request and the total", all(c[0] == rid and c[4] == b.get(11) for c in chunks))
        want = LONG.replace(" ", "\\u{a0}")
        check("the full text is the command, the no-break space spelled out, the line break kept", full == want)
        check("result ok after the last chunk", r and r[3].get(0) is True and r.get(2) == i)
        i = await new.cmd(OP_DETAIL, sid, rid + 1, 0)
        r = await new.rx(lambda m: m.get(0) in (MT_DETAIL, MT_RESULT), 3)
        check("detail for another rid: stale, no text", r and r[0] == MT_RESULT and r[3].get(1) == 0)
        i = await new.cmd(OP_DETAIL, sid, rid, 0)
        await new.rx(result_for(i), 3)
        i = await new.cmd(OP_APPROVE, sid, rid, hold=1250)
        r = await new.rx(result_for(i), 3)
        check("approve after reading: ok", r and r[3].get(0) is True)
        await h.done(5)
        out = json.loads(h.out) if h.out else {}
        check("the hook prints the allow decision", out.get("hookSpecificOutput", {}).get("decision", {}).get("behavior") == "allow")
        check(f"and returns at once ({h.took:.1f} s)", h.took is not None and h.took < 10)
        c = await new.rx(clear_of(rid), 3)
        check("cleared as answered on the device", c and c[3].get(1) == 1)

        # ── 2. the window runs out: no decision, the terminal asks; a late answer is stale ──
        window_file(6)
        h = Hook(permission("npm test"))
        a = await new.rx(attention, 5)
        rid = a[3][0] if a else 0
        check(f"the setting: win {a[3].get(10) if a else None} ms", a and a[3].get(10) == 6000)
        check("a whole text has no len", a and 11 not in a[3])
        c = await new.rx(clear_of(rid), 9)
        check("clear expired when the window ends", c and c[3].get(1) == 2)
        await h.done(5)
        check(f"the hook ends with no output after about 6 s ({h.took:.1f} s)", h.out == "" and h.took is not None and 5.5 < h.took < 9.5)
        i = await new.cmd(OP_APPROVE, sid, rid, hold=1250)
        r = await new.rx(result_for(i), 3)
        check("an approve after expiry: stale", r and r[3].get(0) is False and r[3].get(1) == 0)
        i = await new.cmd(OP_DETAIL, sid, rid, 0)
        r = await new.rx(result_for(i), 3)
        check("detail after expiry: stale", r and r[3].get(1) == 0)

        # ── 3. Open on PC hands it back at once ──
        window_file(60)
        h = Hook(permission("make deploy"))
        a = await new.rx(attention, 5)
        rid = a[3][0] if a else 0
        await old.rx(attention, 3)
        t0 = time.time()
        await new.cmd(OP_FOCUS, sid, rid)
        c = await new.rx(clear_of(rid), 3)
        check("Open on PC: clear handed_back (4)", c and c[3].get(1) == 4)
        c10 = await old.rx(clear_of(rid), 3)
        check("the 0.10 device hears cancelled (3) instead", c10 and c10[3].get(1) == 3)
        await h.done(5)
        check(f"the hook ends with no decision right away ({h.took:.1f} s)", h.out == "" and time.time() - t0 < 3)

        # ── 4. the window fits the hook's own Claude Code timeout ──
        settings(8)
        h = Hook(permission("make release"))
        a = await new.rx(attention, 5)
        rid = a[3][0] if a else 0
        check(f"a hook timeout of 8 s gives a 5 s window ({a[3].get(10) if a else None} ms)", a and a[3].get(10) == 5000)
        await new.cmd(OP_FOCUS, sid, rid)
        await new.rx(clear_of(rid), 3)
        await h.done(5)
        settings(330)

        # ── 5. a hook killed mid-window (Claude Code's own timeout) takes its card off the watch ──
        h = Hook(permission("make clean"))
        a = await new.rx(attention, 5)
        rid = a[3][0] if a else 0
        time.sleep(0.3)
        h.p.kill()
        c = await new.rx(clear_of(rid), 4)
        check("a killed hook: the card is cleared within seconds", c is not None)

        # ── 6. an older hook names no window: held no longer than its own wait ──
        line = json.dumps({"agent": "claude", "event": "PermissionRequest", "ts": int(time.time()), "cwd": PROJECT,
                           "payload": json.loads(permission("ls")), "await": {"wait_ms": 2000}})
        sk = socket.socket(socket.AF_UNIX)
        sk.connect(f"{HOME}/hook.sock")
        sk.sendall((line + "\n").encode())
        sk.shutdown(socket.SHUT_WR)
        a = await new.rx(attention, 5)
        check(f"older hook: window = its own 2 s ({a[3].get(10) if a else None} ms)", a and a[3].get(10) == 2000)
        t0 = time.time()
        sk.settimeout(6)
        reply = b""
        while not reply.endswith(b"\n"):
            chunk = sk.recv(4096)
            if not chunk: break
            reply += chunk
        check(f"older hook: one decision line, nothing before it ({reply!r})", reply == b'{"decision":"none"}\n')
        check(f"after about 2 s ({time.time() - t0:.1f} s)", time.time() - t0 < 4)
        sk.close()

        # the old device never got a key it cannot decode
        await old.rx(lambda m: False, 0.5)
        check("0.10 device: no detail, no window keys, no handed_back",
              not any(m.get(0) == MT_DETAIL for m in old.got)
              and not any(m.get(0) == MT_ATT and any(k in m[3] for k in (9, 10, 11)) for m in old.got)
              and not any(m.get(0) == MT_CLEAR and m[3].get(1) == 4 for m in old.got))

asyncio.run(main())
daemon.send_signal(signal.SIGTERM)
daemon.wait(5)
print("scratch:", HOME, CLAUDE, PROJECT)
sys.exit(0 if ok else 1)
