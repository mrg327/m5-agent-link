# Standing rules end to end (docs/design/STANDING-RULES.md): a Claude Code PermissionRequest that carries
# permission_suggestions, piped into the real `agentlink-hook` -> agentlinkd -> fake devices on WebSocket, one
# speaking proto 0.13 and one 0.12. Checks what each device is offered (the rule, its scope and breadth, or
# nothing), that a grant needs the longer hold and the hash of the very rule offered, what the hook prints back
# for a grant (one suggestion, verbatim, nothing broader), the audit line, a request whose suggestions are all
# too broad or a mode change (no rule offered, nothing saved), a grant on the wrong request, and a grant after the
# window ran out. It starts its own daemon on another port with a scratch AGENTLINK_HOME and a scratch Claude
# Code settings directory; the user's daemon and settings are not touched, and no rule is written anywhere
# (the hook only prints what Claude Code would apply). Scratch directories are left in /tmp (printed at the end).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/standing_rules_e2e.py "$PWD/.."     # the repo root
#
# Takes about twenty seconds. Needs a Unix.
import asyncio, json, os, signal, subprocess, sys, tempfile, threading, time
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="alr", dir="/tmp")  # short: a unix socket path is at most 108 bytes
CLAUDE = tempfile.mkdtemp(prefix="alrc", dir="/tmp")  # the "user's" Claude Code settings, for the hook timeout
PROJECT = tempfile.mkdtemp(prefix="alrp", dir="/tmp")  # the agent's working directory
PORT = 7896
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
           CLAUDE_CONFIG_DIR=CLAUDE, AGENTLINK_KEEP_AWAKE="off", AGENTLINK_PRESENCE="off",
           PATH=f"{BIN}:{os.environ['PATH']}")
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE", "AGENTLINK_REMOTE", "AGENTLINK_WAIT_MS",
                                      "AGENTLINK_ANSWER_WINDOW_S"):
        del env[k]
hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])
open(f"{CLAUDE}/settings.json", "w").write(json.dumps({"hooks": {"PermissionRequest": [{"hooks": [
    {"type": "command", "command": "agentlink-hook claude PermissionRequest", "timeout": 330}]}]}}))
SETTINGS_BEFORE = open(f"{CLAUDE}/settings.json").read()

MT_ATT, MT_CLEAR, MT_CMD, MT_RESULT = 6, 7, 8, 9
OP_APPROVE = 3
ERR_STALE, ERR_DENIED = 0, 1

NARROW = {"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "npm test:*"}],
          "behavior": "allow", "destination": "localSettings"}
MODE = {"type": "setMode", "mode": "acceptEdits", "destination": "session"}
EVERYWHERE = {"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "npm test:*"}],
              "behavior": "allow", "destination": "userSettings"}
WHOLE_TOOL = {"type": "addRules", "rules": [{"toolName": "Bash"}], "behavior": "allow", "destination": "session"}
STAR = {"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "*"}], "behavior": "allow",
        "destination": "session"}
SHARED = {"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "npm test:*"}],
          "behavior": "allow", "destination": "projectSettings"}

def fnv(text, scope, wide):
    h = 0x811C9DC5
    for b in text.encode() + bytes([0, scope, 1 if wide else 0]):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h

def window_file(secs):
    open(f"{HOME}/answer_window", "w").write(f"{secs}\n")

def permission(session, cmd, suggestions):
    return json.dumps({"session_id": session, "cwd": PROJECT, "hook_event_name": "PermissionRequest",
                       "permission_mode": "default", "tool_name": "Bash", "tool_input": {"command": cmd},
                       "permission_suggestions": suggestions})

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
    def decision(self):
        return (json.loads(self.out) if self.out else {}).get("hookSpecificOutput", {}).get("decision", {})

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

class Dev:
    def __init__(self, ws, minor):
        self.ws, self.minor, self.next_id = ws, minor, 10
    async def hello(self):
        h = cbor2.loads(hello_vec)
        h[3][0] = [0, self.minor]
        await self.ws.send(cbor2.dumps(h))
    async def rx(self, want, timeout):
        end = time.time() + timeout
        while time.time() < end:
            try: m = cbor2.loads(await asyncio.wait_for(self.ws.recv(), end - time.time()))
            except asyncio.TimeoutError: break
            if want(m): return m
        return None
    async def approve(self, sid, rid, hold, rh=None):
        self.next_id += 1
        b = {0: OP_APPROVE, 1: sid, 2: rid, 4: hold}
        if rh is not None: b[5] = rh
        await self.ws.send(cbor2.dumps({0: MT_CMD, 1: self.next_id, 3: b}))
        r = await self.rx(lambda m: m.get(0) == MT_RESULT and m.get(2) == self.next_id, 3)
        return r[3] if r else {}

def attention(m): return m.get(0) == MT_ATT
def clear_of(rid): return lambda m: m.get(0) == MT_CLEAR and m[3].get(0) == rid

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

async def session(dev, name):
    subprocess.run([f"{BIN}/agentlink-hook", "claude", "SessionStart"], env=env, cwd=PROJECT, timeout=10,
                   input=json.dumps({"session_id": name, "cwd": PROJECT, "hook_event_name": "SessionStart"}), text=True)
    s = await dev.rx(lambda m: m.get(0) == 4, 3)
    return s[3][0] if s else 0

async def main():
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w13, websockets.connect(f"ws://127.0.0.1:{PORT}/") as w12:
        new, old = Dev(w13, 13), Dev(w12, 12)
        for d in (new, old):
            await d.hello()
            await d.rx(lambda m: m.get(0) == 3, 3)  # snapshot
        sid = await session(new, "e2e")
        check("the hook made a session", sid > 0)
        window_file(60)

        # ── 1. what each device is offered: the narrowest acceptable suggestion, or nothing ──
        h = Hook(permission("e2e", "npm test", [MODE, EVERYWHERE, SHARED, NARROW, WHOLE_TOOL]))
        a = await new.rx(attention, 5)
        b = a[3] if a else {}
        rid = b.get(0)
        check(f"0.13: the rule as Claude Code saves it: {b.get(13)!r}", b.get(13) == "Bash(npm test:*)")
        check(f"0.13: scope 1 = this project (localSettings), not all projects or the shared file: {b.get(14)}", b.get(14) == 1)
        check("0.13: marked wide (it ends in a wildcard)", b.get(15) is True)
        check("0.13: Approve / Deny / Focus, no 'don't ask again' option", b.get(6) == [0, 1, 2] and 5 not in b)
        a12 = await old.rx(attention, 3)
        b12 = a12[3] if a12 else {}
        check("0.12: the same request without keys 13-15 and without options",
              a12 and not any(k in b12 for k in (13, 14, 15)) and 5 not in b12 and b12.get(6) == [0, 1, 2])
        rh = fnv("Bash(npm test:*)", 1, True)
        r = await new.approve(sid, rid, 1250, rh)
        check("a grant with the approve-length hold: denied", r.get(0) is False and r.get(1) == ERR_DENIED)
        r = await new.approve(sid, rid, 1600, fnv("Bash(npm test:*)", 0, True))
        check("a grant naming another scope (the hash of the rule shown for this session): denied",
              r.get(0) is False and r.get(1) == ERR_DENIED)
        r = await new.approve(sid, rid, 1600, fnv("Bash(*)", 1, True))
        check("a grant naming a broader rule: denied", r.get(0) is False and r.get(1) == ERR_DENIED)
        r = await new.approve(sid, rid, 1600, rh)
        check("the rule's own hash and the grant hold: ok", r.get(0) is True)
        await h.done(5)
        d = h.decision()
        check("the hook allows", d.get("behavior") == "allow")
        check(f"and passes back exactly the one suggestion, verbatim: {d.get('updatedPermissions')}",
              d.get("updatedPermissions") == [NARROW])
        printed = h.out or ""
        check("nothing broader in the hook output: no setMode, no userSettings, no projectSettings, no whole-tool rule",
              "setMode" not in printed and "userSettings" not in printed and "projectSettings" not in printed
              and '"toolName":"Bash"}' not in printed.replace(" ", ""))
        c = await new.rx(clear_of(rid), 3)
        check("cleared as answered on the device", c and c[3].get(1) == 1)
        audit = [json.loads(l) for l in open(f"{HOME}/audit.jsonl")]
        rec = [x for x in audit if x.get("kind") == "standing_rule"]
        check("one standing_rule audit line", len(rec) == 1)
        if rec:
            m = rec[0]["msg"]
            check(f"audit: rule, scope, destination, session, time: {m}",
                  m.get("rule") == "Bash(npm test:*)" and m.get("scope") == "this project"
                  and m.get("destination") == "localSettings" and m.get("session") == "claude:e2e"
                  and rec[0].get("ts") and rec[0].get("rid") == rid)
            check("audit: no command output, no tool input", "tool_input" not in json.dumps(rec[0]))
        check("no settings file was touched (the hook only prints what Claude Code applies)",
              open(f"{CLAUDE}/settings.json").read() == SETTINGS_BEFORE
              and not os.path.exists(f"{PROJECT}/.claude"))

        # ── 2. only broad rules or a mode change suggested: no rule offered, a grant saves nothing ──
        h = Hook(permission("e2e", "npm test", [MODE, WHOLE_TOOL, STAR, EVERYWHERE]))
        a = await new.rx(attention, 5)
        b = a[3] if a else {}
        rid = b.get(0)
        check("whole-tool, '*', a mode change and all projects: no rule offered", a and not any(k in b for k in (13, 14, 15)))
        r = await new.approve(sid, rid, 1600, fnv("Bash(*)", 0, True))
        check("a grant anyway: denied, the request stays open", r.get(0) is False and r.get(1) == ERR_DENIED)
        r = await new.approve(sid, rid, 1250)
        check("a plain approve: ok", r.get(0) is True)
        await h.done(5)
        d = h.decision()
        check("the hook allows once, with no updatedPermissions", d.get("behavior") == "allow" and "updatedPermissions" not in d)

        # ── 3. two requests open at once: a grant names its own request's rule, never the other's ──
        sid2 = await session(new, "e2e-2")
        OTHER = {"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "make lint"}],
                 "behavior": "allow", "destination": "session"}
        h1 = Hook(permission("e2e", "npm test", [NARROW]))
        a1 = await new.rx(attention, 5)
        h2 = Hook(permission("e2e-2", "make lint", [OTHER]))
        a2 = await new.rx(attention, 5)
        r1, r2 = a1[3][0], a2[3][0]
        check(f"the second request: {a2[3].get(13)!r} for this session, exact", a2[3].get(13) == "Bash(make lint)"
              and a2[3].get(14) == 0 and a2[3].get(15) is False)
        r = await new.approve(sid2, r2, 1600, rh)
        check("the first request's rule on the second request: denied", r.get(0) is False and r.get(1) == ERR_DENIED)
        r = await new.approve(sid2, r2, 1600, fnv("Bash(make lint)", 0, False))
        check("its own rule: ok", r.get(0) is True)
        await h2.done(5)
        check("the second hook passes back only its own suggestion", h2.decision().get("updatedPermissions") == [OTHER])
        check("the first request is still open", h1.took is None)

        # ── 4. the window runs out: the grant is stale, the hook prints nothing ──
        window_file(5)
        r = await new.approve(sid, r1, 1600, rh)  # (r1 still open in a 60 s window: grant it to free the session)
        await h1.done(5)
        h = Hook(permission("e2e", "npm test", [NARROW]))
        a = await new.rx(attention, 5)
        rid = a[3][0] if a else 0
        c = await new.rx(clear_of(rid), 9)
        check("clear expired when the window ends", c and c[3].get(1) == 2)
        r = await new.approve(sid, rid, 1600, rh)
        check("a grant after expiry: stale", r.get(0) is False and r.get(1) == ERR_STALE)
        await h.done(5)
        check("the hook ends with no output: the terminal asks", h.out == "")
        audit = [json.loads(l) for l in open(f"{HOME}/audit.jsonl")]
        check("three standing_rule audit lines in all (sections 1, 3, 4's first)",
              len([x for x in audit if x.get("kind") == "standing_rule"]) == 3)

asyncio.run(main())
daemon.send_signal(signal.SIGTERM)
daemon.wait(5)
print("scratch:", HOME, CLAUDE, PROJECT)
sys.exit(0 if ok else 1)
