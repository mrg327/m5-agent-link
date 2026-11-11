# Jobs end to end (docs/design/JOBS.md): a real bash in a pty with the shell-init snippet -> agentlink-hook ->
# agentlinkd -> a fake proto-0.9 device on WebSocket. Checks surfacing, Stop (a real SIGINT), exit status,
# dismiss, the estimate, hidden and interactive commands, and that no command text reaches the daemon's files.
# It starts its own daemon on another port with a scratch AGENTLINK_HOME; the user's daemon is not touched.
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets python daemon/tools/jobs_e2e.py "$PWD/.."     # the repo root
#
# Takes about a minute. Needs bash and a Unix.
import asyncio, json, os, pty, subprocess, sys, tempfile, time, shutil, signal
import cbor2, websockets

REPO = sys.argv[1]
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="alh", dir="/tmp")  # short: a unix socket path is at most 108 bytes
PORT = 7899
SHELL_HOME = tempfile.mkdtemp(prefix="alhs", dir="/tmp")
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}",
           AGENTLINK_JOB_MIN_S="1", PATH=f"{BIN}:{os.environ['PATH']}")
for k in list(env):
    if k.startswith("KITTY") or k in ("TMUX", "TMUX_PANE"): del env[k]
hello = cbor2.loads(bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"]))
hello[3][0] = [0, 9]

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)
snippet = subprocess.run([f"{BIN}/agentlink", "shell-init", "bash"], env=env, capture_output=True, text=True).stdout
INIT = f"{SHELL_HOME}/init.sh"
open(INIT, "w").write(snippet)
pid, fd = pty.fork()
if pid == 0:
    os.chdir(REPO)
    os.execve("/bin/bash", ["bash", "--norc", "--noprofile", "-i"], dict(env, PS1="$ ", HOME=SHELL_HOME, HISTCONTROL="ignoreboth"))
def sh(line): os.write(fd, (line + "\n").encode())
def drain():
    import select
    while select.select([fd], [], [], 0)[0]:
        try: os.read(fd, 65536)
        except OSError: break
sh(f"source {INIT}")

got = []
async def main():
    async with websockets.connect(f"ws://127.0.0.1:{PORT}/") as ws:
        mid = [1]
        async def send(t, b):
            mid[0] += 1
            await ws.send(cbor2.dumps({0: t, 1: mid[0], 3: b}))
        await ws.send(cbor2.dumps(hello))
        async def rx(want, timeout):
            end = time.time() + timeout
            while time.time() < end:
                try: m = cbor2.loads(await asyncio.wait_for(ws.recv(), end - time.time()))
                except asyncio.TimeoutError: break
                drain()
                got.append(m)
                print("  rx", {k: v for k, v in m.items()})
                if want(m): return m
            return None
        ok = True
        def check(name, cond):
            nonlocal ok
            print(("PASS " if cond else "FAIL ") + name); ok &= bool(cond)
        is_sess = lambda m: m.get(0) == 4
        # 1. a short command never shows
        sh("true"); 
        # 2. a long one shows after the threshold, with the command line; Stop interrupts it and the card goes
        sh("sleep 30")
        m = await rx(lambda m: is_sess(m) and m[3].get(1) == 3, 5)
        check("job surfaces as agent shell, state work", m and m[3].get(2) == 1)
        check("sum is the command line, tool the program", m and m[3].get(9) == "sleep 30" and m[3].get(6) == "sleep")
        sid = m[3][0] if m else 0
        await send(8, {0: 6, 1: sid, 4: 300})
        r = await rx(lambda m: m.get(0) not in (4,) and 2 in m, 3)
        check("cancel without a hold is refused", r and r[3].get(0) is False)
        t = time.time()
        await send(8, {0: 6, 1: sid, 4: 1250})
        g = await rx(lambda m: m.get(3) == {0: sid} and m.get(0) != 4, 5)
        check("Stop: the card is removed (gone) within 5 s, no done/error", g is not None and time.time() - t < 5)
        check("no done/error delta for the interrupted job", not any(is_sess(x) and x[3].get(2) in (3, 4) for x in got))
        # 3. a failing job: error + exit
        sh("sleep 2; (exit 7)")
        m = await rx(lambda m: is_sess(m) and m[3].get(2) == 3, 8)
        check("failed job: state error with exit 7", m and m[3].get(20) == 7)
        sid = m[3][0] if m else sid
        # rerun: needs a hold; in a pty without kitty there is no typing driver
        await send(8, {0: 17, 1: sid, 4: 300})
        r = await rx(lambda m: 2 in m and m.get(0) != 4, 3)
        check("rerun without a hold is refused", r and r[3].get(0) is False)
        await send(8, {0: 17, 1: sid, 4: 1250})
        r = await rx(lambda m: 2 in m and m.get(0) != 4, 5)
        check("rerun answers (ok, or unsupported without kitty)", r is not None)
        print("  rerun result:", r and r[3])
        # dismiss
        await send(8, {0: 1, 1: sid})
        g = await rx(lambda m: m.get(3) == {0: sid} and m.get(0) != 4, 3)
        check("ack removes the failed job's card", g is not None)
        # 4. estimate: the same successful command three times; the third carries eta
        for i in range(3):
            got.clear()
            sh("sleep 6")
            m = await rx(lambda m: is_sess(m) and m[3].get(2) == 1, 5)
            d = await rx(lambda m: is_sess(m) and m[3].get(2) == 4, 10)
            check(f"run {i+1}: surfaces and finishes done with exit 0", m and d and d[3].get(20) == 0)
            if i == 2: check("third run carries eta ~6 s", m and 5 <= m[3].get(19, 0) <= 8)
            else: check(f"run {i+1}: no estimate yet", m and m[3].get(19, 0) == 0)
            await send(8, {0: 1, 1: m[3][0]}); await rx(lambda x: x.get(0) not in (4,) and x.get(3) == {0: m[3][0]}, 2)
        # 4b. a line kept out of the shell's history (leading space, ignoreboth) is never reported
        got.clear()
        sh(" sleep 3"); await rx(lambda m: False, 5)
        check("a history-hidden line never surfaces", not any(is_sess(x) and x[3].get(1) == 3 for x in got))
        # 5. interactive programs never show
        got.clear()
        sh("less /etc/hostname"); await rx(lambda m: False, 3); os.write(fd, b"q")
        check("less never surfaces", not any(is_sess(x) and x[3].get(1) == 3 for x in got))
        return ok
ok = asyncio.run(main())
sh("exit"); time.sleep(0.5)
daemon.send_signal(signal.SIGTERM); daemon.wait(5)
blob = ""
for root, _, files in os.walk(HOME):
    for f in files:
        try: blob += open(os.path.join(root, f), errors="replace").read()
        except Exception: pass
leak = [w for w in ("sleep 30", "sleep 6", "exit 7", "less /etc") if w in blob]
print(("PASS " if not leak else "FAIL ") + f"no command text under AGENTLINK_HOME (trace, audit, history): {leak}")
print("files:", sorted(os.listdir(HOME)))
shutil.rmtree(SHELL_HOME, ignore_errors=True)
print(open(os.path.join(HOME, "job-history.json")).read()[:300] if os.path.exists(os.path.join(HOME, "job-history.json")) else "no history file")
shutil.rmtree(HOME, ignore_errors=True)
sys.exit(0 if ok and not leak else 1)
