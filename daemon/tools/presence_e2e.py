# Presence-aware alerts and keep-awake end to end (docs/design/PRESENCE.md): the real `agentlink-hook` and
# `agentlink`, a scratch agentlinkd, fake devices on WebSocket (one speaking proto 0.12, one 0.11) and an
# injected presence source: AGENTLINK_PRESENCE_FILE (TEST ONLY), a file the daemon reads on every question
# instead of asking the system, here `state=present focus=yes power=ac` and the like.
#
# Checks: present + focused gives the hush flag and a 2 s window; present elsewhere a 10 s window and a buzz;
# away, locked and unknown the full 60 s window and a buzz (unknown = as before presence existed); an explicit
# window setting wins; the presence setting `off` restores the old behaviour; a finish while the terminal is
# in front hushes the finished buzz, unless Notify is armed; the 0.11 device never sees a new key; `agentlink
# status` says what the daemon believes; the sleep inhibitor is taken when a session works and released when
# it is done, on battery under the `ac` setting, with the setting `off`, and when the daemon exits (checked in
# `systemd-inhibit --list` when that exists). It starts its own daemon on another port with a scratch
# AGENTLINK_HOME; the user's daemon is not touched. Scratch directories are left in /tmp (printed at the end).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/presence_e2e.py "$PWD/.."     # the repo root
#
# Takes about a minute. Needs a Unix.
import asyncio, json, os, shutil, signal, subprocess, sys, tempfile, threading, time
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="alp", dir="/tmp")  # short: a unix socket path is at most 108 bytes
CLAUDE = tempfile.mkdtemp(prefix="alpc", dir="/tmp")
PROJECT = tempfile.mkdtemp(prefix="alpp", dir="/tmp")
PORT = 7899
PRESENCE = f"{HOME}/presence_override"
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
           AGENTLINK_PRESENCE_FILE=PRESENCE, CLAUDE_CONFIG_DIR=CLAUDE, PATH=f"{BIN}:{os.environ['PATH']}")
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE", "AGENTLINK_REMOTE", "AGENTLINK_WAIT_MS", "AGENTLINK_ANSWER_WINDOW_S",
                                      "AGENTLINK_PRESENCE", "AGENTLINK_KEEP_AWAKE"):
        del env[k]
hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])

MT_SNAPSHOT, MT_SESSION, MT_ATT, MT_CLEAR, MT_CMD, MT_RESULT = 3, 4, 6, 7, 8, 9
OP_DENY, OP_NOTIFY = 4, 8
ST_WORK, ST_DONE = 1, 4
HAVE_INHIBIT = shutil.which("systemd-inhibit") is not None and sys.platform.startswith("linux")

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

def presence(state, focus="unknown", power="ac"):
    open(PRESENCE, "w").write(f"state={state} focus={focus} power={power}\n")

def setting(name, text):
    path = f"{HOME}/{name}"
    if text is None:
        if os.path.exists(path): os.remove(path)
    else:
        open(path, "w").write(text + "\n")

def permission(cmd, sid="e2e"):
    return json.dumps({"session_id": sid, "cwd": PROJECT, "hook_event_name": "PermissionRequest",
                       "permission_mode": "default", "tool_name": "Bash", "tool_input": {"command": cmd},
                       "permission_suggestions": []})

def event(name, payload=None):
    p = {"session_id": "e2e", "cwd": PROJECT, "hook_event_name": name}
    p.update(payload or {})
    subprocess.run([f"{BIN}/agentlink-hook", "claude", name], env=env, cwd=PROJECT, timeout=10, input=json.dumps(p), text=True)

class Hook:
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
    async def cmd(self, op, sid, rid=None, arg=None):
        self.next_id += 1
        b = {0: op, 1: sid}
        if rid is not None: b[2] = rid
        if arg is not None: b[3] = arg
        await self.ws.send(cbor2.dumps({0: MT_CMD, 1: self.next_id, 3: b}))
        return self.next_id

def attention(m): return m.get(0) == MT_ATT
def clear_of(rid): return lambda m: m.get(0) == MT_CLEAR and m[3].get(0) == rid
def state_of(sid, st): return lambda m: m.get(0) == MT_SESSION and m[3].get(0) == sid and m[3].get(2) == st

def inhibitors():
    """Lines of `systemd-inhibit --list` that the daemon's lock would show (who agentlink)."""
    if not HAVE_INHIBIT: return None
    out = subprocess.run(["systemd-inhibit", "--list", "--no-pager"], capture_output=True, text=True).stdout
    return sum(1 for l in out.splitlines() if l.split()[:1] == ["agentlink"])

def status():
    r = subprocess.run([f"{BIN}/agentlink", "status", "--json"], env=env, capture_output=True, text=True, timeout=10)
    return json.loads(r.stdout)

def status_text():
    return subprocess.run([f"{BIN}/agentlink", "status"], env=env, capture_output=True, text=True, timeout=10).stdout

async def wait_for(pred, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if pred(): return True
        await asyncio.sleep(0.2)
    return pred()

baseline = inhibitors()
presence("unknown")
daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

async def ask(new, old, cmd, deny=True):
    """Runs one permission hook; returns (attention body, the old device's body, the hook)."""
    h = Hook(permission(cmd))
    a = await new.rx(attention, 6)
    b = a[3] if a else {}
    a11 = await old.rx(attention, 3)
    if deny and b:
        await new.cmd(OP_DENY, SID, b[0])
        await new.rx(clear_of(b[0]), 3)
        await h.done(5)
    return b, (a11[3] if a11 else {}), h

async def main():
    global SID
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w12, websockets.connect(f"ws://127.0.0.1:{PORT}/") as w11:
        new, old = Dev(w12, 12), Dev(w11, 11)
        for d in (new, old):
            await d.hello()
            await d.rx(lambda m: m.get(0) == MT_SNAPSHOT, 3)
        event("SessionStart")
        s = await new.rx(lambda m: m.get(0) == MT_SESSION, 3)
        SID = s[3][0] if s else 0
        check("the hook made a session", SID > 0)
        setting("keep_awake", "off")  # the keep-awake part comes last

        # ── 1. the table, window and buzz, every row ──
        presence("present", "yes")
        b, b11, h = await ask(new, old, "npm test", deny=False)
        check(f"present + terminal focused: hush on the request ({b.get(12)})", b.get(12) is True)
        check(f"... and a short window ({b.get(10)} ms = 2 s)", b.get(10) == 2000)
        check("the card is still shown with its actions (approve, deny, focus)", b.get(6) == [0, 1, 2])
        check("the 0.11 device gets the request without key 12", b11 and 12 not in b11 and b11.get(10) == 2000)
        c = await new.rx(clear_of(b[0]), 5)
        check("it expires on its own: clear expired (2)", c and c[3].get(1) == 2)
        await h.done(5)
        check(f"the hook ends with no decision after about 2 s ({h.took:.1f} s)", h.out == "" and 1.5 < h.took < 4.5)

        presence("present", "no")
        b, b11, h = await ask(new, old, "npm run lint")
        check(f"present, another window: no hush ({b.get(12)}), window {b.get(10)} ms = 10 s", 12 not in b and b.get(10) == 10000)
        presence("present", "unknown")
        b, _, _ = await ask(new, old, "npm run lint 2")
        check(f"present, focus unknown: as elsewhere (no hush, 10 s)", 12 not in b and b.get(10) == 10000)
        for state in ("away", "locked", "unknown"):
            presence(state, "yes")
            b, _, _ = await ask(new, old, f"make {state}")
            check(f"{state}: no hush, the full 60 s window ({b.get(10)} ms), as before presence", 12 not in b and b.get(10) == 60000)
        os.remove(PRESENCE)
        b, _, _ = await ask(new, old, "make nothing")
        check("no source at all (file missing): as before", 12 not in b and b.get(10) == 60000)

        # ── 2. settings: an explicit window wins; presence off restores the old behaviour ──
        presence("present", "yes")
        setting("answer_window", "30")
        b, _, _ = await ask(new, old, "make explicit")
        check(f"an explicit window wins ({b.get(10)} ms = 30 s) ...", b.get(10) == 30000)
        check("... the buzz still follows presence (hush)", b.get(12) is True)
        setting("answer_window", None)
        setting("presence", "off")
        b, _, _ = await ask(new, old, "make off")
        check(f"presence off: no hush and 60 s ({b.get(10)} ms)", 12 not in b and b.get(10) == 60000)
        st = status()["presence"]
        check(f"status says so ({st['state']}, enabled {st['enabled']})", st["state"] == "off" and st["enabled"] is False)
        setting("presence", None)

        # ── 3. status shows what the daemon believes ──
        presence("present", "yes")
        st = status()["presence"]
        check(f"status: present via {st['source']}", st["state"] == "present" and st["source"] == "override-file")
        check("status: the session's terminal is focused", any(s["sid"] == SID and s["focus"] == "yes" for s in st["sessions"]))
        txt = status_text()
        check("agentlink status prints presence and keep awake lines", "presence: present (via override-file" in txt and "keep awake:" in txt)
        presence("locked")
        check("status: locked", status()["presence"]["state"] == "locked")

        # ── 4. a finish: hushed in front of the terminal, unless Notify is armed ──
        setting("keep_awake", "off")
        presence("present", "yes")
        event("UserPromptSubmit", {"prompt": "go"})
        await new.rx(state_of(SID, ST_WORK), 3)
        event("Stop")
        m = await new.rx(state_of(SID, ST_DONE), 3)
        check(f"done while the terminal is in front: key 26 hush ({m[3].get(26) if m else None})", m and m[3].get(26) is True)
        m11 = await old.rx(state_of(SID, ST_DONE), 3)
        check("the 0.11 device gets the done without key 26", m11 and 26 not in m11[3])
        presence("present", "no")
        event("UserPromptSubmit", {"prompt": "go"})
        await new.rx(state_of(SID, ST_WORK), 3)
        event("Stop")
        m = await new.rx(state_of(SID, ST_DONE), 3)
        check("done while elsewhere: no hush", m and 26 not in m[3])
        presence("away")
        event("UserPromptSubmit", {"prompt": "go"})
        await new.rx(state_of(SID, ST_WORK), 3)
        event("Stop")
        m = await new.rx(state_of(SID, ST_DONE), 3)
        check("done while away: no hush", m and 26 not in m[3])
        presence("present", "yes")
        event("UserPromptSubmit", {"prompt": "go"})
        await new.rx(state_of(SID, ST_WORK), 3)
        await new.cmd(OP_NOTIFY, SID, arg=1)
        await asyncio.sleep(0.3)
        event("Stop")
        m = await new.rx(state_of(SID, ST_DONE), 3)
        check("armed with Notify: the user asked to be told, so no hush", m and 26 not in m[3])

        # ── 5. keep awake ──
        presence("present", "yes", power="ac")
        setting("keep_awake", "ac")
        k = status()["keep_awake"]
        check(f"idle: nothing held ({k['state']})", k["state"] == "idle" and k["busy"] is False)
        if HAVE_INHIBIT: check("idle: no agentlink lock in systemd-inhibit --list", inhibitors() == baseline)
        event("UserPromptSubmit", {"prompt": "work"})
        held = await wait_for(lambda: status()["keep_awake"]["state"] == "held", 8)
        k = status()["keep_awake"]
        check(f"a session works on AC under 'ac': held via {k['how']}", held and k["mode"] == "ac" and k["on_ac"] is True)
        if HAVE_INHIBIT:
            check("the lock is in systemd-inhibit --list (idle only, block)", inhibitors() == baseline + 1)
            out = subprocess.run(["systemd-inhibit", "--list", "--no-pager"], capture_output=True, text=True).stdout
            mine = [l for l in out.splitlines() if l.split()[:1] == ["agentlink"]]
            check("... and it holds idle, not sleep or the lid", all(" idle " in l and "sleep" not in l and "lid" not in l for l in mine))
        check("status text names it", "keep awake: held" in status_text())
        presence("present", "yes", power="battery")
        await wait_for(lambda: status()["keep_awake"]["state"] == "idle", 8)
        k = status()["keep_awake"]
        check(f"unplugged under 'ac': released ({k['state']}, on_ac {k['on_ac']})", k["state"] == "idle" and k["on_ac"] is False)
        if HAVE_INHIBIT: check("... and gone from the list", inhibitors() == baseline)
        setting("keep_awake", "always")
        held = await wait_for(lambda: status()["keep_awake"]["state"] == "held", 8)
        check("under 'always' on battery: held", held)
        setting("keep_awake", "off")
        await wait_for(lambda: status()["keep_awake"]["state"] == "idle", 8)
        check("the setting 'off': released", status()["keep_awake"]["state"] == "idle" and status()["keep_awake"]["mode"] == "off")
        setting("keep_awake", "always")
        await wait_for(lambda: status()["keep_awake"]["state"] == "held", 8)
        event("Stop")
        released = await wait_for(lambda: status()["keep_awake"]["state"] == "idle", 8)
        check("the session finishes: released", released and status()["keep_awake"]["busy"] is False)
        if HAVE_INHIBIT: check("... and gone from the list", inhibitors() == baseline)
        # a waiting session counts too (waiting on the user), then the daemon exits with the lock held
        h = Hook(permission("make wait"))
        a = await new.rx(attention, 6)
        held = await wait_for(lambda: status()["keep_awake"]["state"] == "held", 8)
        check("a session waiting for the user holds it", held)
        if a:
            await new.cmd(OP_DENY, SID, a[3][0])
            await h.done(5)
        event("UserPromptSubmit", {"prompt": "again"})
        await wait_for(lambda: status()["keep_awake"]["state"] == "held", 8)

asyncio.run(main())
t0 = time.time()
daemon.send_signal(signal.SIGTERM)
try:
    daemon.wait(8)
except subprocess.TimeoutExpired:
    daemon.kill()
    daemon.wait()
gone = inhibitors()
check(f"the daemon exits on SIGTERM ({time.time() - t0:.1f} s) with the lock still wanted", daemon.returncode is not None)
if HAVE_INHIBIT:
    time.sleep(0.5)
    check("... and the lock is released with it (nothing left in systemd-inhibit --list)", inhibitors() == baseline)
else:
    print("SKIP no systemd-inhibit on this machine: lock list not checked")
print("scratch:", HOME, CLAUDE, PROJECT)
sys.exit(0 if ok else 1)
