"""The giant letters that scroll across the start of the intro: LIGHTENING FORCE -> HECHO CON PiXELS.

The letters are plane A, drawn from intro archive 0x19000 (loaded at ROM 0xC828):
- entry 0 -> RAM FF9000: 256 'metatiles' of 4x4 tile words (32 bytes, row by row; the
  game adds 0x500 to each word), and at +0x3058 the letter stream: 8 metatile numbers
  per 32-pixel strip, top to bottom. Code at 0xCA4C reads it two bytes a frame and
  sends each finished 4-column strip to plane A by DMA.
- entry 1 -> VRAM 0xA000: the tiles (0x500-0x5FF; 0x500 is empty, the stars of plane B
  are 0x5E3 on).
Each letter is a 'tube': along the row or the column, the 7 pixels from the letter's
edge inwards are colours 2,3,4,5,6,7,15 and the inside is empty (render()).

The new text keeps the letters the font already has (H, E, C, O, N, L, pixel for pixel)
and builds P (R without its leg), i (I with a dot), X and S (from C) with the same
outline. Entries 0 and 1 are also used by the title screen (ROM 0x115E4), so they are
not changed in place: the intro loads them from a copy of the archive header whose
entries 0 and 1 point to new uncompressed streams (build()).
"""
import tf4_unpack
import pixels_logo

ARCHIVE = 0x19000
STREAM = 0x3058            # letter stream in entry 0
STRIPS = 68                # strips the intro shows (the original text ends at strip 67)
TILE0 = 0x500
# 0x501-0x5E2: letter tiles; 0x5E3 on are the stars. 0x510/0x511/0x514/0x515/0x5D2/0x5E2 are
# also drawn outside the letters (the metatiles the stream does not use, the plane A fill)
KEEP_TILES = {0x10, 0x11, 0x14, 0x15, 0xD2, 0xE2}
FREE_TILES = [n for n in range(1, 0xE3) if n not in KEEP_TILES]
BAND = [2, 3, 4, 5, 6, 7, 15]
TEXT = 'HECHO CON PiXELS'
GAP, SPACE, LEFT = 14, 46, 27  # pixels between letters, between words, before the first

# x range of each letter in the original text (pixels from the start of the stream)
ORIG = dict(L=(27, 210), I=(224, 274), G=(288, 418), H=(432, 562), T=(568, 730), E=(736, 866),
            N=(880, 1010), F=(1408, 1570), O=(1584, 1714), R=(1728, 1858), C=(1872, 2002))
TOP, BOTTOM = 16, 183      # rows of the letters


def _entries(rom):
    e0 = bytearray(tf4_unpack.dec(rom, tf4_unpack.entry(rom, ARCHIVE, 0))[0])
    e1 = bytearray(tf4_unpack.dec(rom, tf4_unpack.entry(rom, ARCHIVE, 1))[0])
    return e0, e1


def original_text(e0, e1, strips=STRIPS):
    """Colour indices of the original letter strip, 256 rows x strips*32."""
    W = strips*32
    px = [[0]*W for _ in range(256)]
    for s in range(strips):
        for k in range(8):
            mt = e0[STREAM + s*8 + k]
            for r in range(4):
                for c in range(4):
                    w = int.from_bytes(e0[mt*32 + (r*4 + c)*2:mt*32 + (r*4 + c)*2 + 2], 'big')
                    n = w & 0x7FF
                    for y in range(8):
                        for x in range(8):
                            b = e1[n*32 + y*4 + x//2]
                            X = 7 - x if w & 0x800 else x
                            Y = 7 - y if w & 0x1000 else y
                            px[k*32 + r*8 + Y][s*32 + c*8 + X] = b >> 4 if x % 2 == 0 else b & 15
    return px


def render(sil):
    """Tube outline of a silhouette (1 inside): distance to the outside along the row or the
    column, whichever is shorter; 1..7 -> BAND, deeper is empty."""
    H, W = len(sil), len(sil[0])
    out = [[0]*W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if not sil[y][x]:
                continue
            best = 8
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                k = 1
                while k < best:
                    X, Y = x + dx*k, y + dy*k
                    if not (0 <= X < W and 0 <= Y < H) or not sil[Y][X]:
                        break
                    k += 1
                best = min(best, k)
            if best <= 7:
                out[y][x] = BAND[best - 1]
    return out


def silhouette(glyph):
    """Inside of a tube-outlined glyph: the drawn pixels plus the empty areas that touch the
    inner (colour 15) side of the tube rather than the outer (colour 2) side."""
    H, W = len(glyph), len(glyph[0])
    sil = [[1 if glyph[y][x] else 0 for x in range(W)] for y in range(H)]
    seen = [[False]*W for _ in range(H)]
    for y0 in range(H):
        for x0 in range(W):
            if glyph[y0][x0] or seen[y0][x0]:
                continue
            q, pts, inner, outer, edge = [(x0, y0)], [], 0, 0, False
            seen[y0][x0] = True
            while q:
                x, y = q.pop(); pts.append((x, y))
                if x in (0, W - 1) or y in (0, H - 1):
                    edge = True
                for X, Y in ((x+1, y), (x-1, y), (x, y+1), (x, y-1)):
                    if 0 <= X < W and 0 <= Y < H:
                        if glyph[Y][X] == 0:
                            if not seen[Y][X]:
                                seen[Y][X] = True; q.append((X, Y))
                        elif glyph[Y][X] == 15:
                            inner += 1
                        elif glyph[Y][X] == 2:
                            outer += 1
            if not edge and inner > outer:
                for x, y in pts:
                    sil[y][x] = 1
    return sil


def glyphs(orig):
    """Colour indices of every letter of TEXT, rows 0-255, cropped to the letter."""
    crop = lambda a, b: [row[a:b] for row in orig]
    g = {k: crop(*v) for k, v in ORIG.items()}
    H = len(orig)

    # P: the stem and the top hook of R, closed underneath by a bar like R's middle bar
    r = g['R']; Wr = len(r[0])
    stem = 42                                   # R's stem is x 0-41, then its other parts
    hook_end, bar_end = 88, 107                 # rows: end of the top hook, end of the middle bar
    rs = silhouette(r)
    sil = [[1 if x >= stem and (y <= hook_end and rs[y][x] or TOP + 73 <= y <= bar_end) else 0
            for x in range(Wr)] for y in range(H)]
    bowl = render(sil)
    g['P'] = [[r[y][x] if x < stem else bowl[y][x] for x in range(Wr)] for y in range(H)]

    # i: I with its top cap lowered and a square dot above
    I = g['I']; W = len(I[0])
    i = [[0]*W for _ in range(H)]
    dot_bottom, cap = TOP + 33, TOP + 42
    dot = render([[1 if TOP <= y <= dot_bottom else 0 for x in range(W)] for y in range(H)])
    for y in range(H):
        if y <= dot_bottom:
            i[y] = dot[y][:]
        elif cap <= y < cap + 8:
            i[y] = I[TOP + y - cap][:]
        elif y >= cap + 8:
            i[y] = I[y][:]
    g['i'] = i

    # X: two diagonal strokes, as wide (along the row) as the stems
    W = len(g['H'][0]); sw = stem
    sil = [[0]*W for _ in range(H)]
    for y in range(TOP, BOTTOM + 1):
        t = (y - TOP) / (BOTTOM - TOP)
        a = round(t*(W - sw))
        for x in range(a, a + sw):
            sil[y][x] = 1
            sil[y][W - 1 - x] = 1
    g['X'] = render(sil)

    # S: the top half of C, the same turned round for the bottom half, and a middle bar
    c = silhouette(g['C']); W = len(c[0])
    mid = (TOP + BOTTOM + 1)//2
    thick = next(y for y in range(TOP, H) if not c[y][W//2]) - TOP          # bar thickness
    sil = [[0]*W for _ in range(H)]
    for y in range(TOP, mid):
        for x in range(W):
            if c[y][x]:
                sil[y][x] = 1
                sil[TOP + BOTTOM - y][W - 1 - x] = 1
    for y in range(mid - thick//2, mid - thick//2 + thick):
        for x in range(W):
            sil[y][x] = 1
    g['S'] = render(sil)
    return g


def compose(g, text=TEXT, strips=STRIPS):
    W = strips*32
    px = [[0]*W for _ in range(256)]
    x = LEFT
    for k, ch in enumerate(text):
        if ch == ' ':
            x += SPACE - GAP
            continue
        gl = g[ch]
        for y in range(256):
            for dx, v in enumerate(gl[y]):
                if v:
                    px[y][x + dx] = v
        x += len(gl[0]) + GAP
    assert x - GAP <= W, 'text too long'
    return px


def build(rom):
    """(entry 0, entry 1) for the intro with the new text."""
    e0, e1 = _entries(rom)
    orig = original_text(e0, e1, 80)
    px = compose(glyphs(orig))
    used_mt = sorted(set(e0[STREAM:STREAM + STRIPS*8]))

    tiles = {bytes(32): 0}          # tile data -> number (with flips)
    for n in sorted(KEEP_TILES):
        tiles.setdefault(bytes(e1[n*32:n*32 + 32]), n)
    free = iter(FREE_TILES)
    def tile_word(data):
        rows = [data[i*4:i*4 + 4] for i in range(8)]
        flipx = lambda rs: [bytes(((b & 15) << 4) | (b >> 4) for b in r[::-1]) for r in rs]
        for flags, rs in ((0, rows), (0x800, flipx(rows)), (0x1000, rows[::-1]), (0x1800, flipx(rows)[::-1])):
            n = tiles.get(b''.join(rs))
            if n is not None:
                return 0x8000 | flags | n
        n = next(free)
        tiles[data] = n
        e1[n*32:n*32 + 32] = data
        return 0x8000 | n

    # metatile 0 (all words 0) is also what the game fills plane A with: empty cells keep it
    metas = {(0x8000,)*16: 0}
    slots = iter(m for m in used_mt if m != 0)
    stream = bytearray()
    for s in range(STRIPS):
        for k in range(8):
            words = []
            for r in range(4):
                for c in range(4):
                    x0, y0 = s*32 + c*8, k*32 + r*8
                    data = bytes((px[y0 + y][x0 + x] << 4) | px[y0 + y][x0 + x + 1]
                                 for y in range(8) for x in range(0, 8, 2))
                    words.append(tile_word(data))
            key = tuple(words)
            if key not in metas:
                metas[key] = next(slots)
                e0[metas[key]*32:metas[key]*32 + 32] = b''.join(w.to_bytes(2, 'big') for w in words)
            stream.append(metas[key])
    e0[STREAM:STREAM + len(stream)] = stream
    return bytes(e0), bytes(e1), px, len(tiles) - 1, len(metas)


def install(rom, at):
    """Write the new entries as uncompressed streams at ROM address at, with an archive
    header of their own, and make the intro (ROM 0xC828) load entries 0 and 1 from it."""
    e0, e1, _, _, _ = build(rom)
    s0, s1 = pixels_logo.literal_stream(e0), pixels_logo.literal_stream(e1)
    head = bytearray(rom[ARCHIVE:ARCHIVE + 2 + 4*rom[ARCHIVE + 1]])
    head[2:6] = len(head).to_bytes(4, 'big')
    head[6:10] = (len(head) + len(s0)).to_bytes(4, 'big')
    for i in range(2, rom[ARCHIVE + 1]):          # the other entries: still the original ones
        o = int.from_bytes(rom[ARCHIVE + 2 + 4*i:ARCHIVE + 6 + 4*i], 'big')
        head[2 + 4*i:6 + 4*i] = (ARCHIVE + o - at).to_bytes(4, 'big', signed=True)
    blob = bytes(head) + s0 + s1
    rom[at:at + len(blob)] = blob
    assert rom[0xC828:0xC82E] == bytes.fromhex('41F900019000')       # lea $19000.l,a0
    rom[0xC828:0xC82E] = bytes.fromhex('41F9') + at.to_bytes(4, 'big')
    return len(blob)
