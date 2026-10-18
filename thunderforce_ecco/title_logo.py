"""Title logo: LIGHTENING FORCE -> HECHO CON AMOR, in the intro animation and on the title.

Archive 0x1C100 (loaded at ROM 0xC95C) holds the logo:
- entry 0: 64 'metatiles' of 4x4 tile words (tile numbers 0-127, the game adds the base)
  and at +0x800 a 16x8 grid of metatile numbers, column by column: plane A, 64x32 cells.
- entry 2: its 128 tiles (tile 0 empty). The logo sits on cells 5-34, rows 3-13.
- entry 1: the same for the 'extruded' logo the intro shows on plane B while it zooms in
  (320x128 pixels, tiles 0x400 on: words 0-0x1FF), entry 3: its 512 tiles.
The intro first stretches the flat logo (plane A, raster effect), then shows the extruded
one with palette cycling, then the title shows plane A with the chrome palette. All of
them come from these four entries, so the new text appears in every step.

Letters: H, E, N come from line 1 and R, C from line 2, pixel for pixel; A, O and M are
stems of the T joined by bars (the font's own O reads as an A at this size). The extrusion is rebuilt from
the new outline: layer k is the logo scaled about the vanishing point by 1/INV[k-1], drawn
in colour 15-k behind the nearer layers (measured on the original image).
"""
import tf4_unpack

ARCHIVE = 0x1C100
LOGO_X, LOGO_Y = 5*8, 3*8      # logo image (240x88) on plane A, in pixels
TOP1, TOP2 = 9, 49             # top row of the letters of line 1 and line 2 in the logo image
ROWS = 28                      # letter height (the tips included)
# letters in the logo image: (x0, x1, top row)
SRC = dict(H=(61, 82, TOP1), E=(117, 138, TOP1), N=(145, 166, TOP1), T=(87, 114, TOP1),
           R=(117, 139, TOP2), C=(145, 166, TOP2))
T_STEM = 95                    # the T's stem: columns 95-101
LINES = [('HECHO CON', TOP1), ('AMOR', TOP2)]
GAP, SPACE = 6, 12
# extrusion (plane B, 320x128): vanishing point and 1/scale of each layer
EXT_W, EXT_H = 320, 128
CENTER = (149, 66)
INV = [0.981, 0.954, 0.920, 0.874, 0.832, 0.788, 0.743, 0.698, 0.653, 0.608, 0.563, 0.518, 0.473]


def _entry(rom, i):
    return bytearray(tf4_unpack.dec(rom, tf4_unpack.entry(rom, ARCHIVE, i))[0])


def _render(meta, tiles, grid, w, h):
    """Pixels (colour indices) of a metatile map, w x h cells from the top left."""
    px = [[0]*(w*8) for _ in range(h*8)]
    for cy in range(h):
        for cx in range(w):
            m = grid[(cx//4)*8 + cy//4]
            word = int.from_bytes(meta[m*32 + ((cy % 4)*4 + cx % 4)*2:][:2], 'big')
            n = word & 0x7FF
            for y in range(8):
                for x in range(8):
                    b = tiles[n*32 + y*4 + x//2]
                    X = 7 - x if word & 0x800 else x
                    Y = 7 - y if word & 0x1000 else y
                    px[cy*8 + Y][cx*8 + X] = b >> 4 if x % 2 == 0 else b & 15
    return px


def original_logo(rom):
    e0, e2 = _entry(rom, 0), _entry(rom, 2)
    full = _render(e0, e2, e0[0x800:0x880], 40, 16)
    return [row[LOGO_X:LOGO_X + 240] for row in full[LOGO_Y:LOGO_Y + 88]]


def glyphs(logo):
    g = {}
    for ch, (x0, x1, top) in SRC.items():
        g[ch] = [logo[top + y][x0:x1 + 1] for y in range(ROWS)]
    stem = [g['T'][y][T_STEM - SRC['T'][0]:T_STEM - SRC['T'][0] + 7] for y in range(ROWS)]
    colour = [g['T'][y][T_STEM - SRC['T'][0] + 2] for y in range(ROWS)]   # inside, row by row
    def box(W, stems, bars):
        """Stems of the T at the given x, joined by bars (first row, last row) between them."""
        b = [[0]*W for _ in range(ROWS)]
        for y in range(ROWS):
            for x in stems:
                b[y][x:x + 7] = stem[y]
        for y0, y1 in bars:
            for y in range(y0, y1 + 1):
                for x in range(stems[0] + (y0 > 0), stems[-1] + 7 - (y0 > 0)):
                    if y in (y0, y1):
                        if b[y][x] in (0, 15) or y0 == 0 and y == 0:
                            b[y][x] = 15
                    elif x in (0, W - 1):
                        b[y][x] = 15
                    else:
                        b[y][x] = colour[y]
        return b
    g['A'] = box(22, [0, 15], [(0, 5), (9, 13)])        # closed on top, bar in the middle
    g['O'] = box(22, [0, 15], [(0, 5), (19, 24)])       # closed on top and at the bottom
    g['M'] = box(31, [0, 12, 24], [(0, 5)])           # three stems under one bar
    return g


def compose(g):
    logo = [[0]*240 for _ in range(88)]
    for text, top in LINES:
        width = sum(len(g[c][0]) + GAP if c != ' ' else SPACE for c in text) - GAP
        x = (240 - width)//2
        for c in text:
            if c == ' ':
                x += SPACE
                continue
            for y in range(ROWS):
                for dx, v in enumerate(g[c][y]):
                    if v:
                        logo[top + y][x + dx] = v
            x += len(g[c][0]) + GAP
    return logo


def extrusion(logo):
    """The plane B picture: the logo in front, its scaled copies behind."""
    px = [[0]*EXT_W for _ in range(EXT_H)]
    inside = lambda X, Y: 0 <= X - LOGO_X < 240 and 0 <= Y - LOGO_Y < 88 and logo[Y - LOGO_Y][X - LOGO_X]
    cx, cy = CENTER
    for y in range(EXT_H):
        for x in range(EXT_W):
            if inside(x, y):
                px[y][x] = logo[y - LOGO_Y][x - LOGO_X]
                continue
            for k, inv in enumerate(INV, 1):
                if inside(int(cx + (x - cx)*inv + 0.5), int(cy + (y - cy)*inv + 0.5)):
                    px[y][x] = 15 - k
                    break
    return px


def _encode(px, w, h, ntiles):
    """(metatiles + grid, tiles) for a picture of w x h cells at the top left of the map."""
    tiles = {bytes(32): 0}
    tdata = bytearray(32)
    meta = bytearray(0x800)
    grid = bytearray(0x80)
    metas = {(0,)*16: 0}
    for bx in range((w + 3)//4):
        for by in range((h + 3)//4):
            words = []
            for r in range(4):
                for c in range(4):
                    cx, cy = bx*4 + c, by*4 + r
                    if cx >= w or cy >= h:
                        words.append(0); continue
                    data = bytes(px[cy*8 + y][cx*8 + x] << 4 | px[cy*8 + y][cx*8 + x + 1]
                                 for y in range(8) for x in range(0, 8, 2))
                    if data not in tiles:
                        tiles[data] = len(tiles); tdata += data
                    words.append(tiles[data])
            key = tuple(words)
            if key not in metas:
                metas[key] = len(metas)
                meta[metas[key]*32:metas[key]*32 + 32] = b''.join(v.to_bytes(2, 'big') for v in words)
            grid[bx*8 + by] = metas[key]
    assert len(tiles) <= ntiles and len(metas) <= 64, (len(tiles), len(metas))
    return bytes(meta + grid), bytes(tdata + bytes(32*ntiles - len(tdata)))


def build(rom):
    """New entries 0-3 of the archive and the new logo picture."""
    logo = compose(glyphs(original_logo(rom)))
    canvas = [[0]*320 for _ in range(128)]          # plane A, cells 0-39 x 0-15
    for y in range(88):
        canvas[LOGO_Y + y][LOGO_X:LOGO_X + 240] = logo[y]
    e0, e2 = _encode(canvas, 40, 16, 128)
    e1, e3 = _encode(extrusion(logo), 40, 16, 512)
    return [e0, e1, e2, e3], logo
