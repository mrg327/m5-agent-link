# Concept mock-ups for the glyph redesign (GLYPHS.md): 3 directions x 4 screens, drawn with the watch's own fonts and
# colours. Direction B (Sigil) was picked. Scr.sigil() and Scr.minutes() are the reference for ui_sigil / ui_dots.
#   python3 docs/design/glyphs/mock.py out.png
import hashlib, math, sys
from PIL import Image, ImageDraw, ImageFont

# Run from the repository root after `npm install && node gen.mjs` in firmware/tools/fonts (that makes the fonts below).
F = 'firmware/tools/fonts/'
PH = F + 'node_modules/@phosphor-icons/web/src/fill/Phosphor-Fill.ttf'
FIG = F + 'tnum/figtree-%d-tnum.ttf'
S = 2  # supersample
N = 466 * S
C = N // 2
BLUE, YEL, ORG, GRN, RED, GREY = (0x56, 0xb4, 0xe9), (0xf0, 0xe4, 0x42), (0xe6, 0x9f, 0x00), (0x00, 0x9e, 0x73), (0xd5, 0x5e, 0x00), (0x99, 0x99, 0x99)
TXT, TXT2, TXT3, FILL = (255, 255, 255), (0xa6, 0xa6, 0xad), (0x6c, 0x6c, 0x73), (0x2a, 0x2a, 0x2e)
G = dict(work=0xe094, hand=0xe57e, check=0xeba6, x=0xe4f6, pc=0xe560, done=0xe184, ask=0xe16c, moon=0xe330, pulse=0xe000,
         bell=0xe5e8, stop=0xe46e, reply=0xe024, book=0xe0e6, fork=0xe27e, mic=0xe326, dots=0xe204, caret=0xe13a, eye=0xe220)


def ph(px): return ImageFont.truetype(PH, px * S)
def fig(w, px): return ImageFont.truetype(FIG % w, px * S)
def dim(c, k): return tuple(int(v * k) for v in c)


class Scr:
    def __init__(self):
        self.im = Image.new('RGB', (N, N), (0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def glyph(self, name, x, y, px, col):
        if name == 'x':  # the fill family boxes its cross: draw a bold one
            r, w = px * 0.30, max(3, px * 0.16)
            for sx in (1, -1):
                self.d.line([(x - r * sx) * S, (y - r) * S, (x + r * sx) * S, (y + r) * S], fill=col, width=int(w * S))
                for ex, ey in ((x - r * sx, y - r), (x + r * sx, y + r)): self.disc(ex, ey, w / 2, col)
            return
        self.d.text((x * S, y * S), chr(G[name]), font=ph(px), fill=col, anchor='mm')

    def text(self, t, x, y, px, col, w=500, anchor='mm'):
        self.d.text((x * S, y * S), t, font=fig(w, px), fill=col, anchor=anchor)

    def arc(self, r, a0, a1, width, col, cx=233, cy=233):
        b = [(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S]
        self.d.arc(b, a0 - 90, a1 - 90, fill=col, width=width * S)
        for a in (a0, a1):  # round caps
            px = cx + (r - width / 2) * math.sin(math.radians(a)); py = cy - (r - width / 2) * math.cos(math.radians(a))
            self.disc(px, py, width / 2, col)

    def disc(self, x, y, r, col, outline=None, ow=0):
        self.d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=col, outline=outline, width=int(ow * S))

    def pill(self, x0, y0, x1, y1, col):
        self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=(y1 - y0) * S // 2, fill=col)

    def ring(self, segs):  # the session ring at the bezel: [(a0, a1, colour, selected)]
        for a0, a1, col, sel in segs:
            self.arc(226, a0, a1, 9 if sel else 5, col if sel else dim(col, 0.45))

    def sigil(self, name, x, y, r, col, w=None):
        """A project's own mark: three rings of arcs on an 8-sector grid and a core, from the name's hash."""
        h = hashlib.sha256(name.encode()).digest()
        w = w or max(2.5, r / 9)
        for i in range(3):
            bits = h[i] | (1 << (h[3 + i] % 8)) | (1 << (h[6 + i] % 8))
            rr = r * (1 - i * 0.3)
            a = 0
            while a < 8:
                if bits >> a & 1:
                    b = a
                    while b + 1 < 8 and bits >> (b + 1) & 1: b += 1
                    self.arc(rr, a * 45 + 9, (b + 1) * 45 - 9, w, col, x, y)
                    a = b + 1
                else: a += 1
        if h[9] & 1: self.disc(x, y, r * 0.12, col)
        else: self.d.rounded_rectangle([(x - r * .2) * S, (y - w / 2) * S, (x + r * .2) * S, (y + w / 2) * S], radius=int(w * S / 2), fill=col)

    def minutes(self, mins, r, col, x=233, y=233):  # elapsed time as dots: one a minute, a bar every ten
        for i in range(min(mins, 60)):
            a = math.radians(i * 6)
            px, py = x + r * math.sin(a), y - r * math.cos(a)
            self.disc(px, py, 4.2 if (i + 1) % 10 == 0 else 2.2, col)

    def keys(self, a, b):  # what the two buttons do, at the buttons: A lower left (amber), B lower right (red)
        if a:
            self.disc(92, 372, 20, None, ORG, 2.5); self.glyph(a, 92, 372, 22, ORG)
        if b:
            self.disc(374, 372, 20, None, (0xe0, 0x4a, 0x3a), 2.5); self.glyph(b, 374, 372, 22, (0xe0, 0x4a, 0x3a))

    def out(self): return self.im.resize((466, 466), Image.LANCZOS)


RING_W = [(8, 172, BLUE, True), (188, 352, GRN, False)]
RING_P = [(8, 172, ORG, True), (188, 352, BLUE, False)]
CMD = 'npm test -- --watch=false'
SUM = 'Running the test suite after the login refactor'


def summary(s, y, t=SUM, col=TXT2):
    words, lines, cur = t.split(), [], ''
    for w in words:
        if len(cur) + len(w) > 30: lines.append(cur); cur = w
        else: cur = (cur + ' ' + w).strip()
    lines.append(cur)
    for i, l in enumerate(lines[:2]): s.text(l, 233, y + i * 24, 20, col, 400)


# ---------- A "Signal": one giant state glyph; the project keeps its name ----------
def a_home(state):
    s = Scr(); col = BLUE if state == 'work' else ORG
    s.ring(RING_W if state == 'work' else RING_P)
    s.text('webapp', 233, 82, 30, TXT, 600)
    s.glyph('work' if state == 'work' else 'hand', 233, 196, 150, col)
    s.text('1:33' if state == 'work' else '2:29', 233, 296, 28, col, 600)
    summary(s, 332, SUM if state == 'work' else CMD, TXT2 if state == 'work' else TXT)
    s.keys('dots', 'caret'); return s.out()


def a_ask():
    s = Scr(); s.ring(RING_P)
    s.pill(78, 62, 388, 110, FILL); s.text(CMD, 233, 86, 20, TXT, 500)
    s.disc(233, 232, 84, ORG); s.glyph('check', 233, 232, 96, (0, 0, 0))
    s.arc(98, 0, 250, 6, TXT)  # the hold filling
    s.disc(98, 300, 36, FILL); s.glyph('x', 98, 300, 36, TXT2)
    s.disc(368, 300, 36, FILL); s.glyph('pc', 368, 300, 36, TXT2)
    s.keys('pc', 'caret'); return s.out()


def a_menu():
    s = Scr(); s.ring(RING_W)
    items = ['pulse', 'moon', 'bell', 'reply', 'pc', 'stop', 'fork', 'book']
    for i, g in enumerate(items):
        a = math.radians(i * 45); x, y = 233 + 148 * math.sin(a), 233 - 148 * math.cos(a)
        sel = i == 0
        s.disc(x, y, 34 if sel else 28, BLUE if sel else FILL); s.glyph(g, x, y, 34 if sel else 28, (0, 0, 0) if sel else TXT2)
    s.glyph('pulse', 233, 222, 96, BLUE); s.text('webapp', 233, 292, 22, TXT2, 500)
    return s.out()


# ---------- B "Sigil": no names. Each project has a generated mark; time is dots ----------
def b_home(state):
    s = Scr(); col = BLUE if state == 'work' else ORG
    s.ring(RING_W if state == 'work' else RING_P)
    s.sigil('webapp', 233, 66, 26, TXT, 4)
    s.glyph('work' if state == 'work' else 'hand', 233, 218, 136, col)
    s.minutes(14 if state == 'work' else 3, 100, dim(col, 0.85), 233, 218)
    summary(s, 338, SUM if state == 'work' else CMD, TXT2 if state == 'work' else TXT)
    s.keys('dots', 'caret'); return s.out()


def b_ask():
    s = Scr(); s.ring(RING_P)
    s.sigil('webapp', 233, 62, 22, TXT2, 3)
    s.pill(78, 96, 388, 140, FILL); s.text(CMD, 233, 118, 20, TXT, 500)
    s.disc(300, 262, 78, ORG); s.glyph('check', 300, 262, 92, (0, 0, 0)); s.arc(92, 0, 250, 6, TXT, 300, 262)
    s.disc(140, 262, 52, FILL); s.glyph('x', 140, 262, 52, TXT)
    s.disc(233, 380, 26, FILL); s.glyph('pc', 233, 380, 26, TXT2)
    return s.out()


def b_menu():
    s = Scr(); s.ring(RING_W)
    s.sigil('webapp', 233, 233, 52, TXT, 5)
    items = ['pulse', 'moon', 'bell', 'reply', 'pc', 'stop', 'fork', 'book']
    for i, g in enumerate(items):
        a = math.radians(i * 45); x, y = 233 + 150 * math.sin(a), 233 - 150 * math.cos(a)
        sel = i == 0
        s.disc(x, y, 40 if sel else 30, BLUE if sel else FILL); s.glyph(g, x, y, 44 if sel else 30, (0, 0, 0) if sel else TXT2)
    return s.out()


# ---------- C "Totem": the project's mark IS the screen, drawn in the state's colour ----------
def c_home(state):
    s = Scr(); col = BLUE if state == 'work' else ORG
    s.ring(RING_W if state == 'work' else RING_P)
    s.sigil('webapp', 233, 196, 104, col, 11)
    s.disc(318, 276, 40, (0, 0, 0)); s.disc(318, 276, 34, col); s.glyph('work' if state == 'work' else 'hand', 318, 276, 40, (0, 0, 0))
    s.minutes(14 if state == 'work' else 3, 132, dim(col, 0.7), 233, 196)
    summary(s, 356, SUM if state == 'work' else CMD, TXT2 if state == 'work' else TXT)
    return s.out()


def c_ask():
    s = Scr(); s.ring(RING_P)
    # two halves: deny on the left, approve on the right; hold either
    s.d.pieslice([20 * S, 20 * S, 446 * S, 446 * S], -90, 90, fill=dim(ORG, 0.92))
    s.d.pieslice([20 * S, 20 * S, 446 * S, 446 * S], 90, 270, fill=(0x1c, 0x1c, 0x20))
    s.d.rectangle([228 * S, 20 * S, 238 * S, 446 * S], fill=(0, 0, 0))
    s.glyph('check', 334, 262, 124, (0, 0, 0)); s.glyph('x', 132, 262, 96, TXT2)
    s.pill(70, 92, 396, 142, (0, 0, 0)); s.text(CMD, 233, 117, 21, TXT, 500)
    s.sigil('webapp', 233, 52, 18, TXT, 3)
    s.disc(233, 392, 24, (0, 0, 0)); s.glyph('pc', 233, 392, 24, TXT2)
    return s.out()


def c_menu():
    s = Scr(); s.ring(RING_W)
    s.sigil('webapp', 233, 233, 70, dim(BLUE, 0.55), 8)
    items = ['pulse', 'moon', 'bell', 'reply', 'pc', 'stop', 'fork', 'book']
    for i, g in enumerate(items):
        a = math.radians(i * 45 - 0); x, y = 233 + 152 * math.sin(a), 233 - 152 * math.cos(a)
        sel = i == 0
        if sel: s.disc(x, y, 44, BLUE); s.glyph(g, x, y, 50, (0, 0, 0))
        else: s.glyph(g, x, y, 36, TXT2)
    return s.out()


rows = [('A  Signal: one giant state glyph. The project keeps its name; time in digits.', [a_home('work'), a_home('perm'), a_ask(), a_menu()]),
        ('B  Sigil: no names. Each project has its own generated mark; time is dots (one a minute).', [b_home('work'), b_home('perm'), b_ask(), b_menu()]),
        ('C  Totem: the project mark is the screen, in the state colour; approve / deny are the two halves.', [c_home('work'), c_home('perm'), c_ask(), c_menu()])]
cols = ['home: working', 'home: needs approval', 'approval', 'session menu']
sz, pad, lab = 340, 12, 26
W = 4 * (sz + pad) + pad; H = len(rows) * (sz + lab + pad) + pad + 24
sheet = Image.new('RGB', (W, H), (30, 30, 34)); d = ImageDraw.Draw(sheet)
f = ImageFont.truetype(FIG % 600, 17); f2 = ImageFont.truetype(FIG % 500, 14)
for i, c in enumerate(cols): d.text((pad + i * (sz + pad) + 4, 6), c, font=f2, fill=(170, 170, 176))
for r, (title, ims) in enumerate(rows):
    y = 24 + pad + r * (sz + lab + pad)
    d.text((pad + 2, y), title, font=f, fill=(255, 255, 255))
    for i, im in enumerate(ims): sheet.paste(im.resize((sz, sz), Image.LANCZOS), (pad + i * (sz + pad), y + lab))
sheet.save(sys.argv[1]); print(sheet.size)
