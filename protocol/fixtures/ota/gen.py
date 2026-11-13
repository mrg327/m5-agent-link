# /// script
# dependencies = ["detools==0.53.1"]
# ///
"""Delta-OTA fixture (issue ef2c): base.bin -> new.bin, and python.patch made by the reference detools
(sequential, bsdiff, heatshrink window 11 lookahead 10). Device and daemon tests apply every *.patch here to
base.bin and expect new.bin. Regenerate: uv run protocol/fixtures/ota/gen.py (from this directory)."""
# Deterministic synthetic "firmware" pair for delta-OTA tests; patch made by the reference detools.
import random, struct, io, detools, hashlib
r = random.Random(20261113)
words = [r.getrandbits(32) for _ in range(512)]
def blob(n):  # code-like: repeated words with small variation
    out = bytearray()
    while len(out) < n:
        out += struct.pack('<I', r.choice(words) ^ (r.getrandbits(4)))
    return bytes(out[:n])
base = bytearray(blob(49152))
new = bytearray(base)
new[10000:10000] = blob(300)                      # inserted function
new[30000:32048] = blob(2048)                     # rewritten region
for i in range(20000, len(new) - 4, 64):          # shifted pointers
    v = struct.unpack_from('<I', new, i)[0]; struct.pack_into('<I', new, i, (v + 300) & 0xffffffff)
del new[45000:45100]                              # removed code
new += b'\x00' * 7 + blob(93)                     # odd tail
p = io.BytesIO()
detools.create_patch(io.BytesIO(bytes(base)), io.BytesIO(bytes(new)), p, compression='heatshrink', heatshrink_window_sz2=11, heatshrink_lookahead_sz2=10)
open('base.bin','wb').write(base); open('new.bin','wb').write(new); open('python.patch','wb').write(p.getvalue())
assert detools.apply_patch(io.BytesIO(bytes(base)), io.BytesIO(p.getvalue()), io.BytesIO()) == len(new)
print(len(base), len(new), len(p.getvalue()), hashlib.sha256(new).hexdigest())
