# Power report end to end (PROTOCOL §6f, docs/design/POWER.md): a fake watch on WebSocket -> agentlinkd ->
# ~/.agentlink/power-reports.csv -> `agentlink status` and `agentlink power`.
# Checks: one CSV row per report with the header written once; the last report in `agentlink status` (JSON and
# text); `agentlink power` picks the watch over a simulator, uses the reports since the last unplug, and its
# overall rate and per-state rates come back near the synthetic truth; a fit with too little data says so;
# `power.csv` (another tool's file) is never touched. It starts its own daemon on another port with a scratch
# AGENTLINK_HOME (under daemon/target); the user's daemon is not touched.
#
#   cd daemon && cargo build --workspace
#   python3 daemon/tools/power_e2e.py [REPO_ROOT]
#
# Standard library only (its own small CBOR and WebSocket client). Takes about five seconds. Needs a Unix.
import base64, json, os, random, signal, socket, struct, subprocess, sys, tempfile, time

REPO = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = f"{REPO}/daemon/target/debug"
HOME = tempfile.mkdtemp(prefix="pwr", dir=f"{REPO}/daemon/target")  # short: a unix socket path is at most 104 bytes
PORT = 7911
env = dict(os.environ, AGENTLINK_HOME=HOME, AGENTLINK_USB="0", AGENTLINK_BLE="0", AGENTLINK_KEEP_AWAKE="off",
           AGENTLINK_WS_ADDR=f"127.0.0.1:{PORT}", PATH=f"{BIN}:{os.environ['PATH']}")

# ── a small CBOR codec: what the envelopes use (uints, negative ints, bytes, text, arrays, maps, bools) ──
def cbor_enc(v):
    def head(major, n):
        if n < 24: return bytes([major << 5 | n])
        for ai, fmt in ((24, ">B"), (25, ">H"), (26, ">I"), (27, ">Q")):
            if n < 1 << (8 * struct.calcsize(fmt)): return bytes([major << 5 | ai]) + struct.pack(fmt, n)
    if v is True: return b"\xf5"
    if v is False: return b"\xf4"
    if v is None: return b"\xf6"
    if isinstance(v, int): return head(0, v) if v >= 0 else head(1, -1 - v)
    if isinstance(v, bytes): return head(2, len(v)) + v
    if isinstance(v, str): b = v.encode(); return head(3, len(b)) + b
    if isinstance(v, list): return head(4, len(v)) + b"".join(cbor_enc(x) for x in v)
    if isinstance(v, dict): return head(5, len(v)) + b"".join(cbor_enc(k) + cbor_enc(x) for k, x in sorted(v.items()))
    raise TypeError(v)

def cbor_dec(b, i=0):
    ib = b[i]; major, ai = ib >> 5, ib & 31; i += 1
    if major == 7: return {20: False, 21: True, 22: None}[ai], i
    if ai < 24: n = ai
    else:
        size = {24: 1, 25: 2, 26: 4, 27: 8}[ai]; n = int.from_bytes(b[i:i + size], "big"); i += size
    if major == 0: return n, i
    if major == 1: return -1 - n, i
    if major == 2: return bytes(b[i:i + n]), i + n
    if major == 3: return b[i:i + n].decode(), i + n
    if major == 4:
        out = []
        for _ in range(n): x, i = cbor_dec(b, i); out.append(x)
        return out, i
    if major == 5:
        out = {}
        for _ in range(n): k, i = cbor_dec(b, i); x, i = cbor_dec(b, i); out[k] = x
        return out, i
    raise ValueError(ib)

# ── a small WebSocket client (binary frames, client side masked) ──
class Ws:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        resp = b""
        while b"\r\n\r\n" not in resp: resp += self.s.recv(1)
        assert b" 101 " in resp.split(b"\r\n")[0], resp
    def send(self, data):
        mask = os.urandom(4)
        n = len(data)
        hdr = bytes([0x82]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n))
        self.s.sendall(hdr + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(data)))
    def _read(self, n):
        out = b""
        while len(out) < n:
            chunk = self.s.recv(n - len(out))
            if not chunk: raise ConnectionError("closed")
            out += chunk
        return out
    def recv(self, timeout=1.0):
        self.s.settimeout(timeout)
        b0, b1 = self._read(2)
        n = b1 & 127
        if n == 126: n = struct.unpack(">H", self._read(2))[0]
        elif n == 127: n = struct.unpack(">Q", self._read(8))[0]
        data = self._read(n)
        return data if b0 & 15 == 2 else self.recv(timeout)
    def drain(self):
        try:
            while True: self.recv(0.3)
        except (socket.timeout, TimeoutError): pass
    def close(self):
        try: self.s.sendall(bytes([0x88, 0x80]) + os.urandom(4))
        except OSError: pass
        self.s.close()

hello_vec = bytes.fromhex(json.load(open(f"{REPO}/protocol/vectors/hello_sim.json"))["cbor_hex"])

def hello(uid, board, fw, minor=19):
    h, _ = cbor_dec(hello_vec)
    h[3][0] = [0, minor]
    h[3][1][0] = 3 if board == "sim" else 0
    h[3][1][1], h[3][1][2], h[3][1][3] = board, fw, bytes.fromhex(uid)
    return cbor_enc(h)

def connect(uid, board, fw):
    ws = Ws(PORT)
    ws.send(hello(uid, board, fw))
    ws.drain()  # welcome, snapshot
    return ws

# ── synthetic battery: the curve the daemon uses (agentlink-proto battery.rs), inverted ──
OCV = [(3270, 0), (3610, 5), (3690, 10), (3710, 15), (3730, 20), (3750, 25), (3770, 30), (3790, 35), (3800, 40),
       (3820, 45), (3840, 50), (3850, 55), (3870, 60), (3910, 65), (3950, 70), (3980, 75), (4020, 80), (4080, 85),
       (4110, 90), (4150, 95), (4200, 100)]
def mv_of(soc):
    for (v0, p0), (v1, p1) in zip(OCV, OCV[1:]):
        if soc <= p1: return v0 + (v1 - v0) * (soc - p0) / (p1 - p0)
    return 4200

TRUTH = {"on": 40.0, "dim": 20.0, "off": 2.0, "deep": 0.5}  # %/h
mid = [100]
def power(seq, soc, on=0, dim=0, off=0, deep=0, nd=0, chg=False, usb=False, itvl=30000, noise=0):
    mid[0] += 1
    mv = 0 if soc is None else int(round(mv_of(soc) + noise))
    body = {0: mv, 1: mv, 2: 5050 if usb else 0, 3: chg, 4: usb, 5: max(0, min(100, (mv - 3300) // 9)),
            6: on, 7: dim, 8: off, 9: deep, 10: on + dim + off, 11: on + dim + off, 12: itvl, 13: seq, 14: nd}
    return cbor_enc({0: 27, 1: mid[0], 3: body})

def run(*args):
    p = subprocess.run([f"{BIN}/agentlink", *args], env=env, capture_output=True, text=True, timeout=30)
    return p.returncode, p.stdout, p.stderr

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok &= bool(cond)

csv_path = f"{HOME}/power-reports.csv"
other = f"{HOME}/power.csv"
open(other, "w").write("owned by another logger\n")

daemon = subprocess.Popen([f"{BIN}/agentlinkd"], env=env, stdout=open(f"{HOME}/daemon.log", "w"), stderr=subprocess.STDOUT)
try:
    for _ in range(50):
        try: socket.create_connection(("127.0.0.1", PORT), timeout=0.2).close(); break
        except OSError: time.sleep(0.1)

    rc, out, err = run("power")
    check("no log yet: `agentlink power` says so", rc != 0 and "no power reports yet" in err)

    # the watch: two reports while charging on USB, then 60 on battery (5 min each, a 2 h deep sleep every 10th)
    watch = connect("0a0b0c0d0e0f1011", "m5stopwatch", "c040548")
    rng = random.Random(7)
    seq, soc, n_rows = 0, 97.0, 0
    for _ in range(2):
        watch.send(power(seq, soc, on=60000, off=240000, chg=True, usb=True)); seq += 1; n_rows += 1
    for k in range(60):
        on = rng.choice([0, 0, 30000, 90000, 200000])
        dim = rng.choice([0, 30000, 60000, 100000])
        off = 300000 - on - dim
        deep = 7200000 if k % 10 == 5 else 0
        soc -= (TRUTH["on"] * on + TRUTH["dim"] * dim + TRUTH["off"] * off + TRUTH["deep"] * deep) / 3600000
        watch.send(power(seq, soc, on, dim, off, deep, nd=1 if deep else 0, noise=rng.uniform(-3, 3)))
        seq += 1; n_rows += 1
    # a simulator on the same daemon: its reports are logged, but `agentlink power` prefers the watch
    sim = connect("0102030405060708", "sim", "0.1.0-sim")
    sim.send(power(0, 80.0, on=300000, itvl=0)); n_rows += 1
    time.sleep(0.8)

    lines = open(csv_path).read().splitlines() if os.path.exists(csv_path) else []
    check("one CSV row per report, after one header", len(lines) == n_rows + 1)
    header = lines[0].split(",") if lines else []
    check("header names the columns", header[:3] == ["time_unix", "time", "device"] and "ocv_pct" in header
          and sum(1 for l in lines if l.startswith("time_unix")) == 1)
    last_watch = [l for l in lines if ",0a0b0c0d0e0f1011," in l][-1].split(",") if lines else []
    col = lambda name: last_watch[header.index(name)]
    check("a row carries the device, board, transport and figures",
          col("board") == "m5stopwatch" and col("transport") == "ws" and col("seq") == str(seq - 1)
          and col("itvl_us") == "30000" and col("chg") == "0" and float(col("ocv_pct")) > 0)
    check("power.csv is never touched", open(other).read() == "owned by another logger\n")

    rc, out, err = run("--json", "status")
    st = json.loads(out) if rc == 0 else {}
    pw = {p["device"]: p for p in st.get("power", [])}
    check("status keeps the last report of each device", rc == 0 and len(pw) == 2
          and pw.get("0a0b0c0d0e0f1011", {}).get("report", {}).get("seq") == seq - 1)
    rc, out, err = run("status")
    check("status text shows it", rc == 0 and "power (last report):" in out and "m5stopwatch 0a0b0c0d0e0f1011" in out)
    print(out[out.find("power (last report)"):].split("sessions:")[0].rstrip())

    rc, out, err = run("--json", "power")
    a = json.loads(out) if rc == 0 else {}
    print(json.dumps({k: a.get(k) for k in ("window", "overall", "fit")}, indent=None))
    check("`agentlink power` picks the watch, not the simulator", a.get("device") == "0a0b0c0d0e0f1011")
    w = a.get("window") or {}
    check("window: the 60 reports since the unplug", w.get("kind") == "since_unplug" and w.get("reports") == 60)
    hours = sum(5 / 60 + (2 if k % 10 == 5 else 0) for k in range(1, 60))
    check(f"window covers the device time ({hours:.2f} h)", abs((w.get("hours") or 0) - hours) < 0.05)
    o = a.get("overall") or {}
    true_overall = (97.0 - 2 * 0 - soc) / (hours + 5 / 60)
    check(f"overall rate near the truth ({true_overall:.1f} %/h)", abs((o.get("pct_per_h") or 0) - true_overall) < 1.5)
    rates = {s["state"]: s for s in a.get("states", [])}
    for name, tol in (("on", 3), ("dim", 3), ("off", 1), ("deep", 0.3)):
        r = rates.get(name, {})
        # within the tolerance, or three standard errors of the fit (the noise and whole-mV readings set it)
        lim = max(tol, 3 * (r.get("se") or 0))
        check(f"{name}: fitted {r.get('rate_pct_per_h', 0):.2f} ±{r.get('se') or 0:.2f} %/h near {TRUTH[name]}",
              r.get("rate_pct_per_h") is not None and abs(r["rate_pct_per_h"] - TRUTH[name]) < lim)
    check("fit ok", (a.get("fit") or {}).get("ok") is True)

    rc, out, err = run("power")
    print(out.rstrip())
    check("text: battery line, window, overall, per-state table and the caveat",
          rc == 0 and "by the LiPo curve" in out and "since the last unplug" in out and "%/h" in out
          and "deep" in out and "voltage only" in out)

    # too little data: the simulator alone has one report
    rc, out, err = run("power", "--device", "0102")
    check("one report: says there is not enough data", rc == 0 and "not enough data" in out)

    watch.close(); sim.close()
    check("power.csv still untouched at the end", open(other).read() == "owned by another logger\n")
finally:
    daemon.send_signal(signal.SIGTERM); daemon.wait(5)
print("scratch:", HOME)
sys.exit(0 if ok else 1)
