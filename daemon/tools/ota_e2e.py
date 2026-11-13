# Firmware update end to end (PROTOCOL §6c): `agentlink ota` -> agentlinkd -> a fake watch on WebSocket.
# Checks: a whole transfer with one dropped chunk (the daemon rewinds), the sha the device computes, done, the
# restart (disconnect, hello with the new fw) and the CLI's exit 0; a busy device (CLI fails with the message);
# a proto 0.14 device (refused before anything is sent); an unsigned image; that no image byte reaches the
# trace; and the delta path (proto 0.21): a patch against the cached image the watch runs, rebuilt here with the
# reference detools, and the fall back to the whole image when the watch refuses the base. It starts its own
# daemon on another port with a scratch AGENTLINK_HOME (under daemon/target); the user's daemon is not touched.
#
#   cd daemon && cargo build --workspace
#   uv run --with cbor2 --with websockets --with detools==0.53.1 python daemon/tools/ota_e2e.py "$PWD/.."  # the repo root
#
# Takes about twenty seconds. Needs a Unix.
import asyncio, hashlib, json, os, signal, subprocess, sys, tempfile, time
import io
import cbor2, detools, websockets

REPO = os.path.abspath(sys.argv[1])
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="ota", dir=f"{REPO}/daemon/target")  # short: a unix socket path is at most 104 bytes
PORT = 7900
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_KEEP_AWAKE="off",
           AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}", PATH=f"{BIN}:{os.environ['PATH']}")
hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])
NEW_VER = "9.9.9-ota"
CH = 2048

def make_image(signed=True, ver=NEW_VER, size=40 * 4096 + 0):
    d = bytearray(os.urandom(size))
    d[0] = 0xE9
    d[32:36] = bytes([0x32, 0x54, 0xCD, 0xAB])
    d[48:80] = ver.encode().ljust(32, b"\0")
    d[80:112] = b"agentlink".ljust(32, b"\0")
    if signed:
        d[size - 4096], d[size - 4095] = 0xE7, 0x02
    else:
        d[size - 4096], d[size - 4095] = 0, 0
    return bytes(d)

IMG = make_image()
IMG_PATH, UNSIGNED_PATH = f"{HOME}/agentlink.bin", f"{HOME}/unsigned.bin"
open(IMG_PATH, "wb").write(IMG)
open(UNSIGNED_PATH, "wb").write(make_image(signed=False))

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
time.sleep(1.0)

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

def hello(minor, fw, uid):
    h = cbor2.loads(hello_vec)
    h[3][0] = [0, minor]
    h[3][1][2] = fw
    h[3][1][3] = bytes.fromhex(uid)
    return cbor2.dumps(h)

async def cli(*args):
    p = await asyncio.create_subprocess_exec(f"{BIN}/agentlink", *args, env=env, stdout=asyncio.subprocess.PIPE,
                                             stderr=asyncio.subprocess.PIPE)
    return p

async def finish(p, t=60):
    out, err = await asyncio.wait_for(p.communicate(), t)
    return p.returncode, out.decode(), err.decode()

class Watch:
    """A fake watch: answers fw_begin / fw_chunk like the firmware does. `running` is its image (the base of
    a patch)."""
    def __init__(self, ws, uid, running=None):
        self.ws, self.uid, self.running = ws, uid, running
        self.patch = None
        self.mid = 100
        self.buf = bytearray()
        self.begun = None
        self.begins = []
        self.dropped = False
        self.drop_at = 5 * CH
        self.chunks = 0

    async def ack(self, st, nxt, msg=None):
        self.mid += 1
        b = {0: st, 1: nxt}
        if msg: b[2] = msg
        await self.ws.send(cbor2.dumps({0: 26, 1: self.mid, 3: b}))

    async def serve(self, mode="ok"):
        """mode ok: take the image or patch. busy: refuse. nobase: refuse a patch (Base mismatch)."""
        while True:
            try: raw = await self.ws.recv()
            except websockets.ConnectionClosed: return
            m = cbor2.loads(raw)
            if m[0] == 24:
                size = m[3][0]
                self.begins.append(size)
                if size == 0:
                    await self.ack(5, len(self.buf)); continue
                if mode == "busy":
                    await self.ack(2, 0, "recording"); continue
                patch = m[3].get(3)
                if patch and (mode == "nobase" or hashlib.sha256(self.running[:patch[2]]).digest() != patch[1]):
                    self.begins.append("patch refused")
                    await self.ack(3, 0, "Base mismatch"); continue
                self.begun = (size, m[3][1], m[3].get(2))
                self.patch = patch
                self.buf = bytearray()
                await self.ack(0, 0)
            elif m[0] == 25:
                off, data = m[3][0], m[3][1]
                self.chunks += 1
                if off == self.drop_at and not self.dropped:
                    self.dropped = True  # lost on the way: nothing, not even an ack
                    continue
                if off == len(self.buf):
                    self.buf += data
                if len(self.buf) == (self.patch[0] if self.patch else self.begun[0]):
                    if self.patch:
                        out = io.BytesIO()
                        detools.apply_patch(io.BytesIO(self.running[:self.patch[2]]), io.BytesIO(bytes(self.buf)), out)
                        self.received_patch = bytes(self.buf)
                        self.buf = bytearray(out.getvalue())
                    good = hashlib.sha256(self.buf).digest() == self.begun[1]
                    await self.ack(1 if good else 3, len(self.buf), None if good else "sha")
                    return "done"
                await self.ack(0, len(self.buf))

async def connect(minor, fw, uid):
    ws = await websockets.connect(f"ws://127.0.0.1:{PORT}/")
    await ws.send(hello(minor, fw, uid))
    for _ in range(3):  # welcome, snapshot, ...
        try: await asyncio.wait_for(ws.recv(), 1.0)
        except asyncio.TimeoutError: break
    return ws

async def main():
    # an old-protocol device: refused before anything is sent
    old = await connect(14, "0.1.0", "aa00000000000001")
    rc, out, err = await finish(await cli("ota", IMG_PATH))
    check("proto 0.14 device: CLI fails", rc != 0 and "0.15" in err)
    await old.close()
    await asyncio.sleep(0.3)

    rc, out, err = await finish(await cli("ota", "/nonexistent/x.bin"))
    check("missing file: CLI fails", rc != 0)

    # a device with OTA: the unsigned image is refused by the CLI's daemon, before any begin
    ws = await connect(15, "0.1.0", "bb00000000000002")
    w = Watch(ws, "bb00000000000002")
    rc, out, err = await finish(await cli("ota", UNSIGNED_PATH))
    check("unsigned image refused with FIRMWARE.md hint", rc != 0 and "not signed" in err)

    # busy
    task = asyncio.create_task(w.serve("busy"))
    rc, out, err = await finish(await cli("ota", IMG_PATH))
    check("busy device: CLI fails with the device's message", rc != 0 and "busy" in err and "recording" in err)
    check("busy: no chunk was sent", w.chunks == 0)
    await ws.close(); await task

    # the whole transfer, one lost chunk
    ws = await connect(15, "0.1.0", "bb00000000000002")
    w = Watch(ws, "bb00000000000002")
    p = await cli("ota", IMG_PATH)
    r = await w.serve("ok")
    check("device received the whole image intact (sha)", r == "done" and bytes(w.buf) == IMG)
    check("begin carried the version", w.begun and w.begun[2] == NEW_VER)
    check("the dropped chunk was resent (more chunks than the image has)", w.chunks > len(IMG) // CH)
    # a second update while the first is waiting for the restart is refused
    rc2, out2, err2 = await finish(await cli("ota", IMG_PATH))
    check("a second update is refused while one runs", rc2 != 0 and "already running" in err2)
    await ws.close()  # the watch restarts
    await asyncio.sleep(1.5)
    ws2 = await connect(15, NEW_VER, "bb00000000000002")
    rc, out, err = await finish(p)
    check("CLI exits 0 and reports the new version", rc == 0 and NEW_VER in out)
    check("CLI showed progress", "%" in err or "KB" in err)
    await ws2.close()
    await asyncio.sleep(0.3)

    # the watch comes back on the old firmware: rolled back
    ws = await connect(15, "0.1.0", "bb00000000000002")
    w = Watch(ws, "bb00000000000002")
    p = await cli("ota", IMG_PATH)
    w.dropped = True
    await w.serve("ok")
    await ws.close(); await asyncio.sleep(1.0)
    ws2 = await connect(15, "0.1.0", "bb00000000000002")
    rc, out, err = await finish(p)
    check("old fw after the update: CLI fails (rolled back)", rc != 0 and "rolled back" in err)
    await ws2.close()

    # Ctrl-C: the CLI is killed mid-transfer, the daemon sends the stop
    ws = await connect(15, "0.1.0", "bb00000000000002")
    w = Watch(ws, "bb00000000000002")
    w.drop_at = 3 * CH  # then silence: this chunk never gets an answer and nothing else is sent after the window
    w.dropped = False
    p = await cli("ota", IMG_PATH)
    async def silent():
        while True:
            try: raw = await ws.recv()
            except websockets.ConnectionClosed: return
            m = cbor2.loads(raw)
            if m[0] == 24:
                w.begins.append(m[3][0])
                if m[3][0]: await w.ack(0, 0)
    t = asyncio.create_task(silent())
    await asyncio.sleep(1.0)
    p.send_signal(signal.SIGINT)
    await p.wait()
    await asyncio.sleep(1.0)
    check("closing the CLI makes the daemon send fw_begin size 0", w.begins and w.begins[-1] == 0)
    await ws.close(); await t

    # delta (proto 0.21): the watch runs an image this machine has (in fw/, as the firmware build leaves it)
    old_ver, delta_ver = "8.8.8-base", "8.8.9-delta"
    base = bytearray(make_image(ver=old_ver))
    new = bytearray(base)
    new[48:80] = delta_ver.encode().ljust(32, b"\0")
    new[20000:20000 + 3000] = os.urandom(3000)
    new[90000:90000 + 500] = os.urandom(500)
    base, new = bytes(base), bytes(new)
    os.makedirs(f"{HOME}/fw", exist_ok=True)
    open(f"{HOME}/fw/{hashlib.sha256(base).hexdigest()}.bin", "wb").write(base)
    new_path = f"{HOME}/delta.bin"
    open(new_path, "wb").write(new)

    ws = await connect(21, old_ver, "cc00000000000003")
    w = Watch(ws, "cc00000000000003", running=base)
    w.drop_at = 1 * CH
    p = await cli("ota", new_path)
    r = await w.serve("ok")
    check("delta: begin carried a patch against the running image",
          w.patch and w.patch[1] == hashlib.sha256(base).digest() and w.patch[2] == len(base))
    check("delta: the patch is small", w.patch and w.patch[0] < len(new) // 10)
    check("delta: the watch rebuilt the new image (sha)", r == "done" and bytes(w.buf) == new)
    check("delta: chunks carried patch bytes (resent after the drop)",
          w.chunks > w.patch[0] // CH and w.chunks < len(new) // CH)
    await ws.close(); await asyncio.sleep(1.5)
    ws2 = await connect(21, delta_ver, "cc00000000000003")
    rc, out, err = await finish(p)
    check("delta: CLI exits 0 and says it sent a delta", rc == 0 and delta_ver in out and "delta" in err and old_ver in err)
    check("delta: the sent image is kept in fw/", os.path.exists(f"{HOME}/fw/{hashlib.sha256(new).hexdigest()}.bin"))
    await ws2.close(); await asyncio.sleep(0.3)

    # the watch says it runs that version but refuses the base: the whole image follows
    ws = await connect(21, old_ver, "cc00000000000003")
    w = Watch(ws, "cc00000000000003", running=base)
    p = await cli("ota", new_path)
    r = await w.serve("nobase")
    check("Base mismatch: refused, then the whole image", "patch refused" in w.begins and w.patch is None
          and r == "done" and bytes(w.buf) == new)
    await ws.close(); await asyncio.sleep(1.5)
    ws2 = await connect(21, delta_ver, "cc00000000000003")
    rc, out, err = await finish(p)
    check("Base mismatch: CLI exits 0 and reports the fall back", rc == 0 and "Base mismatch" in err and "whole image" in err)
    await ws2.close(); await asyncio.sleep(0.3)

    # nothing of the image in the trace (fw/ is the image cache)
    leak = False
    needle = IMG[5000:5032]
    seen_len = False
    for root, _, files in os.walk(HOME):
        if os.path.basename(root) == "fw": continue
        for f in files:
            if f in ("agentlink.bin", "unsigned.bin", "delta.bin", "hook.sock", "remote.sock"): continue
            data = open(os.path.join(root, f), "rb").read()
            if needle.hex().encode() in data or needle in data: leak = True
            if b"fw_chunk" in data and b'"len"' in data: seen_len = True
    check("no image byte reached the trace", not leak)
    check("fw_chunk lines are in the trace with their length", seen_len)
    audit = open(f"{HOME}/audit.jsonl").read() if os.path.exists(f"{HOME}/audit.jsonl") else ""
    check("audit log has the start and the end", "ota_start" in audit and "ota_end" in audit)

try:
    asyncio.run(main())
finally:
    daemon.send_signal(signal.SIGTERM); daemon.wait(5)
print("scratch:", HOME)
sys.exit(0 if ok else 1)
