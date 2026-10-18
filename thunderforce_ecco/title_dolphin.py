"""Title screen: the big Rynex picture -> the same picture with Ecco instead of the ship.

The picture is entry 4 of archive 0x1C100: 1120 tiles (40x28) loaded to VRAM 0 and shown
on plane B with palette line 3, tile n at cell n (ROM 0xCFDC; also loaded at 0x1122 and
0xF898). The ship is cut out (the area of non-black pixels around it, holes filled) and
filled with black (index 8, like the rest of the sky); then an Ecco frame is drawn over it,
scaled up 4x: the outline enlarged with EPX (scale2x) twice so diagonals stay clean, the
shading smoothed (the sprite's checker dither averaged out) and put back on the picture's
blue ramp with a 4x4 ordered dither.
"""
import os
from PIL import Image, ImageFilter
import tf4_unpack
import pixels_logo

ARCHIVE = 0x1C100
ENTRY = 4
REFS = [0x1122, 0xC95C, 0xCF6E, 0xCFDC, 0xF898]     # lea $1C100.l,a0
W, H = 320, 224
BLACK = 8                       # opaque black of palette line 3 (index 0 is never used)
SHIP_SEEDS = [(150, 140), (60, 125), (290, 80), (283, 100), (275, 70), (272, 68)]
SHIP_BOX = (50, 55, 310, 222)   # the ship's red parts (5, 6, 7, 9) in here go too
FRAME, SCALE, X0, Y0 = 23, 4, 12, 62    # Ecco leaping, top-left corner on the screen
EYE = (56, 27, 2, 1)            # the eye in the frame (x, y, w, h): the smoothing would blur it away
RAMP = [1, 13, 2, 3, 4, 15]     # palette line 3, dark to light blue
OUTLINE = 14
BRIGHT = 1.25
PAL3 = [0x000, 0x088, 0x0D1, 0x163, 0x1F5, 0x003, 0x046, 0x057,
        0x000, 0x0B7, 0x091, 0x0DA, 0x16C, 0x110, 0x048, 0x1FF]   # as the game sets it (BBBGGGRRR)
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def _rgb(c):
    return ((c & 7)*36, (c >> 3 & 7)*36, (c >> 6 & 7)*36)


def load(rom):
    """Picture as 224 rows of colour indices."""
    d = tf4_unpack.dec(rom, tf4_unpack.entry(rom, ARCHIVE, ENTRY))[0]
    px = [[0]*W for _ in range(H)]
    for t in range(W//8 * H//8):
        tx, ty = t % 40, t // 40
        for y in range(8):
            for x in range(8):
                b = d[t*32 + y*4 + x//2]
                px[ty*8 + y][tx*8 + x] = b >> 4 if x % 2 == 0 else b & 15
    return px


def tiles(px):
    out = bytearray()
    for t in range(W//8 * H//8):
        tx, ty = t % 40, t // 40
        for y in range(8):
            for x in range(0, 8, 2):
                out.append(px[ty*8 + y][tx*8 + x] << 4 | px[ty*8 + y][tx*8 + x + 1])
    return bytes(out)


def erase_ship(px):
    dark = lambda x, y: px[y][x] in (0, BLACK)
    ship = [[False]*W for _ in range(H)]
    for seed in SHIP_SEEDS:
        if dark(*seed) or ship[seed[1]][seed[0]]:
            continue
        q = [seed]; ship[seed[1]][seed[0]] = True
        while q:
            x, y = q.pop()
            for X in (x - 1, x, x + 1):
                for Y in (y - 1, y, y + 1):
                    if 0 <= X < W and 0 <= Y < H and not ship[Y][X] and not dark(X, Y):
                        ship[Y][X] = True; q.append((X, Y))
    m = Image.new('L', (W, H))
    m.putdata([255 if ship[y][x] else 0 for y in range(H) for x in range(W)])
    mp = m.filter(ImageFilter.MaxFilter(5)).load()
    # everything the outside can't reach (the ship and the dark gaps inside it) is ship
    out = [[False]*W for _ in range(H)]
    q = [(x, y) for x in range(W) for y in (0, H - 1)] + [(x, y) for y in range(H) for x in (0, W - 1)]
    q = [(x, y) for x, y in q if not mp[x, y]]
    for x, y in q:
        out[y][x] = True
    while q:
        x, y = q.pop()
        for X, Y in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= X < W and 0 <= Y < H and not out[Y][X] and not mp[X, Y]:
                out[Y][X] = True; q.append((X, Y))
    x0, y0, x1, y1 = SHIP_BOX
    for y in range(H):
        for x in range(W):
            red = x0 <= x < x1 and y0 <= y < y1 and px[y][x] in (5, 6, 7, 9) and not (y < 80 and x > 290)
            if not out[y][x] or red:
                px[y][x] = BLACK


def _epx(img):
    w, h = img.size; p = img.load()
    o = Image.new('L', (w*2, h*2)); op = o.load()
    g = lambda x, y: p[x, y] if 0 <= x < w and 0 <= y < h else 0
    for y in range(h):
        for x in range(w):
            P, A, B, C, D = g(x, y), g(x, y - 1), g(x + 1, y), g(x - 1, y), g(x, y + 1)
            e = [P]*4
            if C == A and C != D and A != B: e[0] = A
            if A == B and A != C and B != D: e[1] = B
            if D == C and D != B and C != A: e[2] = C
            if B == D and B != A and D != C: e[3] = D
            op[2*x, 2*y], op[2*x + 1, 2*y], op[2*x, 2*y + 1], op[2*x + 1, 2*y + 1] = e
    return o


def draw_dolphin(px, sprite):
    w, h = sprite.size; sp = sprite.load()
    solid = [[sp[x, y][3] >= 128 for x in range(w)] for y in range(h)]
    # shading without the checker dither: each pixel averaged with its solid neighbours,
    # then spread one pixel outwards so the scaled edge is not darkened
    sm = Image.new('RGB', (w, h)); smp = sm.load()
    for y in range(h):
        for x in range(w):
            if solid[y][x]:
                n = [sp[X, Y] for X, Y in ((x, y), (x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))
                     if 0 <= X < w and 0 <= Y < h and solid[Y][X]]
                smp[x, y] = tuple(sum(c[i] for c in n)//len(n) for i in range(3))
    for _ in range(2):
        nxt = sm.copy(); np_ = nxt.load()
        filled = [[solid[y][x] or smp[x, y] != (0, 0, 0) for x in range(w)] for y in range(h)]
        for y in range(h):
            for x in range(w):
                if not filled[y][x]:
                    for X, Y in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if 0 <= X < w and 0 <= Y < h and filled[Y][X]:
                            np_[x, y] = smp[X, Y]; break
        sm, smp = nxt, np_
    A = Image.new('L', (w, h)); A.putdata([255 if solid[y][x] else 0 for y in range(h) for x in range(w)])
    A = _epx(_epx(A)); W2, H2 = A.size; ap = A.load()
    assert (W2, H2) == (w*SCALE, h*SCALE)
    col = sm.resize((W2, H2), Image.BICUBIC).load()
    lum = lambda c: 0.3*c[0] + 0.59*c[1] + 0.11*c[2]
    rl = [lum(_rgb(PAL3[i])) for i in RAMP]
    for y in range(H2):
        for x in range(W2):
            X, Y = X0 + x, Y0 + y
            if not (0 <= X < W and 0 <= Y < H):
                continue
            if ap[x, y]:
                L = lum(col[x, y])*BRIGHT
                k = 0
                while k < len(rl) - 2 and L > rl[k + 1]:
                    k += 1
                t = max(0, min(1, (L - rl[k])/(rl[k + 1] - rl[k])))
                px[Y][X] = RAMP[k + 1] if t*16 > BAYER[y % 4][x % 4] + 0.5 else RAMP[k]
            elif any(0 <= x + dx < W2 and 0 <= y + dy < H2 and ap[x + dx, y + dy]
                     for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                px[Y][X] = OUTLINE


    ex, ey, ew, eh = EYE
    for y in range(ey*SCALE, (ey + eh)*SCALE):
        for x in range(ex*SCALE + 1, (ex + ew)*SCALE - 1):
            px[Y0 + y][X0 + x] = RAMP[0]
    px[Y0 + ey*SCALE + 1][X0 + ex*SCALE + 2] = 15


def build(rom, frames_dir):
    px = load(rom)
    erase_ship(px)
    draw_dolphin(px, Image.open(os.path.join(frames_dir, 'frame_%03d.png' % FRAME)).convert('RGBA'))
    return px


def preview(px, path, zoom=3):
    im = Image.new('RGB', (W, H)); p = im.load()
    for y in range(H):
        for x in range(W):
            p[x, y] = _rgb(PAL3[px[y][x]])
    im.resize((W*zoom, H*zoom), Image.NEAREST).save(path)


def install(rom, at, frames_dir, more=None):
    """Write a copy of the archive header whose entry 4 is the new picture, and the entries
    in more ({index: data}) also replaced (uncompressed streams right after it), and point
    every load of the archive at it."""
    new = dict(more or {})
    new[ENTRY] = tiles(build(rom, frames_dir))
    n = rom[ARCHIVE + 1]
    head = bytearray(rom[ARCHIVE:ARCHIVE + 2 + 4*n])
    blob = bytearray()
    for i in range(n):
        if i in new:
            o = len(head) + len(blob)
            blob += pixels_logo.literal_stream(new[i])
        else:
            o = ARCHIVE + int.from_bytes(rom[ARCHIVE + 2 + 4*i:ARCHIVE + 6 + 4*i], 'big') - at
        head[2 + 4*i:6 + 4*i] = o.to_bytes(4, 'big', signed=True)
    blob = bytes(head) + blob
    rom[at:at + len(blob)] = blob
    for r in REFS:
        assert rom[r:r + 6] == bytes.fromhex('41F90001C100'), hex(r)
        rom[r + 2:r + 6] = at.to_bytes(4, 'big')
    return len(blob)
