# Last message and questions end to end (docs/design/LAST-MESSAGE.md): Claude Code hook lines piped into the
# real `agentlink-hook` -> agentlinkd -> fake devices on WebSocket, one speaking proto 0.18 and one 0.17.
# Checks that a Stop's last_assistant_message reaches the 0.18 device as lm / ll / lp and can be read in chunks
# bound to the message, cleaned of markdown and emoji; that the 0.17 device gets none of it; that a new prompt
# clears it; that neither the trace nor the audit log holds the message; that AskUserQuestion's PreToolUse no
# longer holds the hook; and that its PermissionRequest is held, shown with `qs` (and without to 0.17), refused
# without a hold or from 0.17, and answered with `cmd answer` into the hook's PermissionRequest allow with
# updatedInput.answers. A dictated answer needs whisper.cpp and is covered by the unit tests. Privacy `meta`
# cannot be set on a running daemon yet, so it is covered by the unit tests too. It starts its own daemon on
# another port with a scratch AGENTLINK_HOME and a scratch Claude Code settings directory; the user's daemon and
# settings are not touched. Scratch directories are left in /tmp (printed at the end).
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/last_message_e2e.py "$PWD/.."     # the repo root
#
# Takes about ten seconds. Needs a Unix.
import asyncio, glob, json, os, signal, subprocess, sys, tempfile, threading, time
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="all", dir="/tmp")  # short: a unix socket path is at most 108 bytes
CLAUDE = tempfile.mkdtemp(prefix="allc", dir="/tmp")  # the "user's" Claude Code settings, for the hook timeout
PROJECT = tempfile.mkdtemp(prefix="allp", dir="/tmp")  # the agent's working directory
PORT = 7893
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

MT_SNAP, MT_UPD, MT_ATT, MT_CLEAR, MT_CMD, MT_RESULT, MT_DETAIL = 3, 4, 6, 7, 8, 9, 10
OP_DETAIL, OP_ANSWER = 7, 19
ERR_STALE, ERR_DENIED, ERR_UNSUPPORTED = 0, 1, 2
LM, LL, LP = 36, 37, 38
FOCUS = 2

SECRET = "zebraplatypus"  # a word of the message that must never reach the logs
MESSAGE = (f"## Done 🎉\n\nThe **{SECRET}** build is green and `cargo test` passes.\n\n"
           "- flashed the watch\n  - checked the boot log\n- ran the e2e ✅\n\n"
           "| Test | Result |\n|---|---|\n| unit | pass |\n| e2e | pass |\n\n"
           "```sh\n" + "\n".join(f"step {i}" for i in range(10)) + "\n```\n\n"
           + ("Notes on the [design](https://example.com/d). " * 4 + "\n") * 80
           + "\n\nShould I open the pull request?")
QUESTIONS = [
    {"question": "Which framework should the dashboard use?", "header": "Framework", "multiSelect": False,
     "options": [{"label": "React", "description": "Component model, big ecosystem"},
                 {"label": "Vue", "description": "Lighter"}]},
    {"question": "Which parts should I set up now?", "header": "Parts", "multiSelect": True,
     "options": [{"label": "Router"}, {"label": "Store"}, {"label": "Tests"}]},
]

def hook(event, payload, timeout=10):
    """agentlink-hook claude <event>, run to its end: (stdout, seconds)."""
    t0 = time.time()
    p = subprocess.run([f"{BIN}/agentlink-hook", "claude", event], env=env, cwd=PROJECT, timeout=timeout,
                       input=json.dumps(dict(payload, session_id="e2e", cwd=PROJECT, hook_event_name=event)),
                       capture_output=True, text=True)
    return p.stdout, time.time() - t0

class Hook:
    """agentlink-hook claude PermissionRequest, running until the daemon releases it."""
    def __init__(self, payload):
        self.p = subprocess.Popen([f"{BIN}/agentlink-hook", "claude", "PermissionRequest"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env, cwd=PROJECT)
        self.p.stdin.write(json.dumps(dict(payload, session_id="e2e", cwd=PROJECT,
                                           hook_event_name="PermissionRequest", permission_mode="default")))
        self.p.stdin.close()
        self.out, self.done_at = None, None
        threading.Thread(target=self._wait, daemon=True).start()
    def _wait(self):
        self.out = self.p.stdout.read()
        self.p.wait()
        self.done_at = time.time()
    async def done(self, timeout):
        end = time.time() + timeout
        while self.done_at is None and time.time() < end:
            await asyncio.sleep(0.05)
        return self.done_at is not None

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
    async def cmd(self, op, sid, rid=None, arg=None, hold=None, picks=None, aids=None):
        self.next_id += 1
        b = {0: op, 1: sid}
        if rid is not None: b[2] = rid
        if arg is not None: b[3] = arg
        if hold is not None: b[4] = hold
        if picks is not None: b[7] = picks
        if aids is not None: b[8] = aids
        await self.ws.send(cbor2.dumps({0: MT_CMD, 1: self.next_id, 3: b}))
        return self.next_id
    async def result(self, i, timeout=3):
        m = await self.rx(lambda m: m.get(0) == MT_RESULT and m.get(2) == i, timeout)
        return m[3] if m else {}

def upd_with(key): return lambda m: m.get(0) == MT_UPD and key in m[3]
def attention(m): return m.get(0) == MT_ATT
def clear_of(rid): return lambda m: m.get(0) == MT_CLEAR and m[3].get(0) == rid

async def read_last(dev, sid, off=0):
    """detail chunks of the session's last message from `off`, and the result."""
    i = await dev.cmd(OP_DETAIL, sid, arg=off)
    chunks = []
    while True:
        m = await dev.rx(lambda m: m.get(0) in (MT_DETAIL, MT_RESULT), 5)
        if m is None: return chunks, {}
        if m[0] == MT_DETAIL: chunks.append(m[3])
        elif m.get(2) == i: return chunks, m[3]

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

async def main():
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as w18, websockets.connect(f"ws://127.0.0.1:{PORT}/") as w17:
        new, old = Dev(w18, 18), Dev(w17, 17)
        for d in (new, old):
            await d.hello()
            await d.rx(lambda m: m.get(0) == MT_SNAP, 3)
        hook("SessionStart", {})
        s = await new.rx(lambda m: m.get(0) == MT_UPD, 3)
        sid = s[3][0] if s else 0
        check("the hook made a session", sid > 0)
        hook("UserPromptSubmit", {"prompt": "build it"})

        # ── 1. Stop: the last message, its id, length and start ──
        hook("Stop", {"last_assistant_message": MESSAGE, "stop_hook_active": False})
        u = await new.rx(upd_with(LM), 3)
        b = u[3] if u else {}
        lm, ll, lp = b.get(LM, 0), b.get(LL, 0), b.get(LP, "")
        check(f"0.18: lm {lm}, ll {ll} in the delta that enters done", lm > 0 and 0 < ll <= 4096 and b.get(2) == 4)
        check(f"lp is the start, one line, <= 160 bytes: {lp!r}", lp.startswith("Done The ") and SECRET in lp
              and "\n" not in lp and len(lp.encode()) <= 160 and "**" not in lp and "🎉" not in lp)
        chunks, r = await read_last(new, sid)
        text = "".join(c[2] for c in chunks)
        check(f"read in {len(chunks)} chunks of <= 1024 bytes, then ok", len(chunks) >= 2 and r.get(0) is True
              and all(len(c[2].encode()) <= 1024 for c in chunks))
        check("every chunk names the message, the session and its length, no rid",
              all(c.get(5) == lm and c.get(1) == sid and c.get(4) == ll and 0 not in c for c in chunks))
        check("the chunks are in order and add up to ll",
              [c[3] for c in chunks] == [sum(len(x[2].encode()) for x in chunks[:k]) for k in range(len(chunks))]
              and len(text.encode()) == ll)
        lines = text.split("\n")
        check("markdown cleaned: heading, bold, code span, bullets, table",
              lines[0] == "Done" and f"The {SECRET} build is green and cargo test passes." in lines
              and "• flashed the watch" in lines and "  • checked the boot log" in lines
              and "• ran the e2e [x]" in lines and "Test · Result" in lines and "unit · pass" in lines
              and not any(l.startswith("|") or l.startswith("#") or "**" in l for l in lines))
        check("the code block: six lines, then the count", "step 5" in lines and "step 6" not in lines
              and "  … 4 more lines" in lines)
        check("links are their text, emoji dropped", "Notes on the design." in text and "https://" not in text
              and "🎉" not in text)
        check("capped at 4096 bytes on a line end, with …", ll <= 4096 and text.endswith("\n…"))
        resume = len(chunks[0][2].encode()) if chunks else 0
        more, r = await read_last(new, sid, resume)
        check("resumed from an offset", r.get(0) is True and more and more[0][3] == resume
              and "".join(c[2] for c in more) == text.encode()[resume:].decode())
        chunks, r = await read_last(new, sid + 50)
        check("another session: stale, no text", not chunks and r.get(1) == ERR_STALE)

        # ── 2. the 0.17 device: none of it ──
        await old.rx(lambda m: False, 0.3)
        check("0.17: no lm / ll / lp in any session message",
              not any(m.get(0) == MT_UPD and any(k in m[3] for k in (LM, LL, LP)) for m in old.got)
              and any(m.get(0) == MT_UPD and m[3].get(2) == 4 for m in old.got))
        chunks, r = await read_last(old, sid)
        check("0.17: detail without a rid is refused as before", not chunks and r.get(1) == ERR_DENIED)

        # ── 3. a new prompt clears it ──
        hook("UserPromptSubmit", {"prompt": "now the docs"})
        u = await new.rx(upd_with(LM), 3)
        check("a new prompt: lm 0, ll 0, lp \"\"", u and (u[3][LM], u[3].get(LL), u[3].get(LP)) == (0, 0, ""))
        chunks, r = await read_last(new, sid)
        check("no message: stale", not chunks and r.get(1) == ERR_STALE)

        # ── 4. AskUserQuestion: its PreToolUse holds nothing ──
        out, took = hook("PreToolUse", {"tool_name": "AskUserQuestion", "tool_input": {"questions": QUESTIONS}})
        check(f"PreToolUse for the question returns at once with no output ({took:.1f} s)", out == "" and took < 3)
        a = await new.rx(attention, 0.5)
        check("and puts no card up", a is None)

        # ── 5. its PermissionRequest is held with the questions ──
        payload = {"tool_name": "AskUserQuestion", "tool_input": {"questions": QUESTIONS}}
        h = Hook(payload)
        a = await new.rx(attention, 5)
        b = a[3] if a else {}
        rid = b.get(0)
        qs = b.get(16) or []
        check("0.18: a question card with both questions", b.get(2) == 1 and b.get(3) == "2 questions" and len(qs) == 2)
        check("qs: headers, labels, descriptions, multi-select",
              len(qs) == 2 and qs[0][0] == "Framework" and [o[0] for o in qs[0][2]] == ["React", "Vue"]
              and qs[0][2][0].get(1) == "Component model, big ecosystem" and 3 not in qs[0]
              and qs[1][3] is True and [o[0] for o in qs[1][2]] == ["Router", "Store", "Tests"])
        check("held with the answer window", b.get(10) == 60000)
        a17 = await old.rx(attention, 3)
        check("0.17: the card without qs, Open on PC only", a17 and 16 not in a17[3] and a17[3].get(6) == [FOCUS])
        i = await old.cmd(OP_ANSWER, sid, rid, hold=1250, picks=[1, 5])
        r = await old.result(i)
        check("0.17: answer unsupported", r.get(1) == ERR_UNSUPPORTED)
        i = await new.cmd(OP_ANSWER, sid, rid, hold=0, picks=[1, 5])
        r = await new.result(i)
        check("a tap (no hold): denied", r.get(0) is False and r.get(1) == ERR_DENIED)
        i = await new.cmd(OP_ANSWER, sid, rid, hold=1250, picks=[3, 5])
        r = await new.result(i)
        check("two options on a single-choice question: denied", r.get(1) == ERR_DENIED)
        i = await new.cmd(OP_ANSWER, sid, rid, hold=1250, picks=[1])
        r = await new.result(i)
        check("one entry for two questions: denied", r.get(1) == ERR_DENIED)
        i = await new.cmd(OP_ANSWER, sid, rid, hold=1250, picks=[0, 5], aids=[77, 0])
        r = await new.result(i)
        check("a recording with no transcript: stale", r.get(1) == ERR_STALE)
        check("the hook is still waiting", h.done_at is None)
        i = await new.cmd(OP_ANSWER, sid, rid, hold=1250, picks=[1, 5])
        r = await new.result(i)
        check("held answer: ok", r.get(0) is True)
        await h.done(5)
        out = json.loads(h.out) if h.out else {}
        hso = out.get("hookSpecificOutput", {})
        dec = hso.get("decision", {})
        upd = dec.get("updatedInput", {})
        check("the hook prints the PermissionRequest allow", hso.get("hookEventName") == "PermissionRequest"
              and dec.get("behavior") == "allow" and "updatedPermissions" not in dec)
        check(f"updatedInput.answers: {upd.get('answers')}", upd.get("answers") == {
            QUESTIONS[0]["question"]: "React", QUESTIONS[1]["question"]: "Router, Tests"})
        check("updatedInput keeps the questions as sent", upd.get("questions") == QUESTIONS)
        c = await new.rx(clear_of(rid), 3)
        check("cleared as answered on the device", c and c[3].get(1) == 1)
        i = await new.cmd(OP_ANSWER, sid, rid, hold=1250, picks=[1, 5])
        r = await new.result(i)
        check("a second answer: stale", r.get(1) == ERR_STALE)

        # ── 6. Open on PC on a question: no decision, the terminal asks ──
        h = Hook(payload)
        a = await new.rx(attention, 5)
        rid = a[3][0] if a else 0
        await new.cmd(0, sid, rid)
        await h.done(5)
        check("Open on PC: the hook ends with no output", h.out == "")

    # ── 7. the logs ──
    time.sleep(0.3)
    trace = "".join(open(f).read() for f in glob.glob(f"{HOME}/trace/*.jsonl"))
    audit = open(f"{HOME}/audit.jsonl").read() if os.path.exists(f"{HOME}/audit.jsonl") else ""
    check(f"the trace never holds the message ({len(trace)} bytes read)", trace and SECRET not in trace)
    check("the audit log has the answer's command but no label",
          '"picks":[1,5]' in audit and "React" not in audit and "Router" not in audit and SECRET not in audit)

asyncio.run(main())
daemon.send_signal(signal.SIGTERM)
daemon.wait(5)
print("scratch:", HOME, CLAUDE, PROJECT)
sys.exit(0 if ok else 1)
