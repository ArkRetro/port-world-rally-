#!/usr/bin/env python3
"""Convert World Rally's tile graphics (4 x 512 KB, 16x16 4bpp, MAME `wrally_tilelayout16`)
into Neo Geo C-ROM format (c1 = planes 0/1, c2 = planes 2/3; 64 bytes per tile per ROM).

Arcade layout (per MAME gfxdecode, region of 2 MB loaded as i13@0 i11@1 i9@0x100000 i7@0x100001):
  planes: { RGN_FRAC(1,2)+8, RGN_FRAC(1,2)+0, 8, 0 }  (bit offsets of plane 3..0 inside the tile)
  x: { 0..7, 256..263 }   y: { 0,16,32,...,240 }   tile stride 64*8 = 512 bits = 64 bytes
  -> tile n: bytes n*64 .. : row y: bits (y*16 + x) for x<8 in the first word pair, x>=8 at +256 bits.
Neo Geo layout (per MAME optimize_helper): tile = 128 bytes of the interleaved c1/c2 stream;
  row y: pixels 0-7 from bytes 0x40+4y (bit x = pixel x), pixels 8-15 from bytes 0x00+4y;
  byte +0 = plane0 (c1), +1 = plane2 (c2), +2 = plane1 (c1), +3 = plane3 (c2).

Usage: gfx2crom.py <i13> <i11> <i9> <i7> <out_c1> <out_c2> [--png tiles.png] [--shadow]
--shadow appends a second set of 16384 tiles (numbers 0x4000+) for the arcade's "shadow" sprites.  Pen p >= 8
selects palette bank p-8 for the pixel below: bank 0 is the normal palette (no change), the game sets bank 1 to
a darker copy and leaves banks 2-7 black.  So pens 0-8 become transparent, pen 9 is kept on a checkerboard only
(50 % dither: a black palette gives a translucent-looking shadow on hardware without alpha blending) and pens
10-15 are kept whole (black).
--light (after --shadow) appends a third set (numbers 0x8000+) for the same sprites in the night stages, where
the game makes bank 0 (the tiles) the darkest and banks 1-7 brighter copies: the headlight beams.  The Neo Geo
cannot recolour what is below, so the beam is an open dither of light pixels: pen p >= 9 (p-8 = how many
steps brighter) lights 1 to 5 pixels in 16 (ordered 4x4 pattern; 5 in 16 in the middle of the beam, which
is pen 15), keeping the pen so
that the runtime's light palette can give each step its own brightness.  The roads stay visible below.
--extra FILE appends the port's own tiles (256 bytes of pens each) from 0xC000.
The C ROMs are padded to a power of two.
"""
import sys


def decode_arcade(region):
    """Return list of tiles; tile = bytes(256) of pens, row-major."""
    ntiles = len(region) // 128      # 64 bytes per tile in each half of the region (2 planes per half)
    half = len(region) // 2
    tiles = []
    for n in range(ntiles):
        base = n * 64
        tile = bytearray(256)
        for y in range(16):
            for x in range(16):
                xoff = x if x < 8 else 256 + (x - 8)
                bit = y * 16 + xoff
                pen = 0
                # plane bit offsets for pens bit 0..3 (MAME lists them MSB first: {half*8+8, half*8, 8, 0})
                for p, poff in enumerate((0, 8, half * 8, half * 8 + 8)):
                    b = base * 8 + bit + poff
                    # bit numbering inside a byte: MAME gfx layouts count bit 0 as the MSB of the byte
                    if (region[b >> 3] >> (7 - (b & 7))) & 1:
                        pen |= 1 << p
                tile[y * 16 + x] = pen
        tiles.append(bytes(tile))
    return tiles


def encode_neogeo(tiles):
    c1 = bytearray(len(tiles) * 64)
    c2 = bytearray(len(tiles) * 64)
    for n, t in enumerate(tiles):
        for y in range(16):
            for half, xbase in ((0x40, 0), (0x00, 8)):
                p0 = p1 = p2 = p3 = 0
                for x in range(8):
                    pen = t[y * 16 + xbase + x]
                    p0 |= (pen & 1) << x
                    p1 |= ((pen >> 1) & 1) << x
                    p2 |= ((pen >> 2) & 1) << x
                    p3 |= ((pen >> 3) & 1) << x
                o = half + 4 * y  # offset in the 128-byte interleaved stream
                # even stream bytes -> c1, odd -> c2 ; stream offset o -> file offset o//2 within the 64-byte tile
                c1[n * 64 + o // 2] = p0
                c2[n * 64 + o // 2] = p2
                c1[n * 64 + (o + 2) // 2] = p1
                c2[n * 64 + (o + 2) // 2] = p3
    return bytes(c1), bytes(c2)


def main():
    a = sys.argv[1:]
    i13, i11, i9, i7, oc1, oc2 = a[:6]
    region = bytearray(0x200000)
    region[0x000000:0x100000:2] = open(i13, 'rb').read()
    region[0x000001:0x100000:2] = open(i11, 'rb').read()
    region[0x100000:0x200000:2] = open(i9, 'rb').read()
    region[0x100001:0x200000:2] = open(i7, 'rb').read()
    tiles = decode_arcade(region)
    if '--text' in a:
        # tiles of small 8-pixel text (the copyright line of the intro photos: --text first-last): in each
        # half (one letter) the column that loses least goes instead of 5 / 11, the rest shifted so that the
        # shrink drops a filler there - whole letters, a pixel less of spacing
        lo, hi = (int(v) for v in a[a.index('--text') + 1].split('-'))
        for n in range(lo, hi + 1):
            t = tiles[n]
            m = bytearray(256)
            for h, drop_at in ((0, 5), (8, 11)):
                def loss(c):
                    p = c - 1 if c > h else c + 1
                    return sum(1 for y in range(16) if t[y * 16 + c] != t[y * 16 + p])
                d = min(range(h, h + 8), key=lambda c: (loss(c), c != drop_at))
                for y in range(16):
                    row = [t[y * 16 + c] for c in range(h, h + 8) if c != d]
                    row.insert(drop_at - h, row[drop_at - h - 1])
                    m[y * 16 + h:y * 16 + h + 8] = bytes(row)
            tiles[n] = bytes(m)
    if '--rescue' in a:
        # the runtime shows the tiles 14 pixels wide (SCB2 horizontal shrink 0xD), which drops source columns
        # 5 and 11 (MAME zoom_x_tables[13]): a one-pixel stroke there (unlike both neighbours) is moved onto the neighbour that belongs
        # to a wider run (right, else left), else onto a background neighbour, so that thin letters and outlines keep it
        out = []
        for n, t in enumerate(tiles):
            m = bytearray(t)
            for y in range(16):
                r = y * 16
                for c, d in ((5, 1), (11, 1)):
                    p = m[r + c]
                    if p == 0 or p == m[r + c - 1] or p == m[r + c + 1]:
                        continue
                    if m[r + c + d] == m[r + c + 2 * d]:
                        m[r + c + d] = p
                    elif m[r + c - d] == m[r + c - 2 * d]:                 # or the left one
                        m[r + c - d] = p
                    elif m[r + c + d] == 0 or m[r + c - d] == 0:           # or onto the background
                        m[r + c + (d if m[r + c + d] == 0 else -d)] = p
            out.append(bytes(m))
        tiles = out
    if '--shadow' in a:
        shadow = []
        for t in tiles:
            m = bytearray(256)
            for i in range(256):
                p = t[i]
                if p >= 10 or (p == 9 and ((i & 1) ^ ((i >> 4) & 1))):
                    m[i] = p
            shadow.append(bytes(m))
        tiles = tiles + shadow
    if '--light' in a:
        # 8x8 ordered dither (the 4x4 pattern refined: same look, finer steps): 1..5 lit pixels in 16 become
        # 3, 6, 9, 12, 15 in 64 - a quarter fewer lit pixels than the 4x4 pattern had
        b4 = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
        bayer = [4 * b4[(y & 3) * 4 + (x & 3)] + (0, 2, 3, 1)[((y >> 2) & 1) * 2 + ((x >> 2) & 1)]
                 for y in range(8) for x in range(8)]
        light = []
        for t in tiles[:16384]:
            m = bytearray(256)
            for i in range(256):
                p = t[i]
                if p >= 9:
                    lit = ((p - 8) * 5 + 3) // 7                                # 1..5 of 16
                    if bayer[((i >> 4) & 7) * 8 + (i & 7)] < round(lit * 4 * 0.75):
                        m[i] = p
            light.append(bytes(m))
        tiles = tiles + light
    if '--extra' in a:
        # the port's own pictures (tools/mklogo.py: the start-up screen), from tile 0xC000
        assert len(tiles) == 0xC000, len(tiles)
        x = open(a[a.index('--extra') + 1], 'rb').read()
        tiles = tiles + [x[i:i + 256] for i in range(0, len(x), 256)]
    while len(tiles) & (len(tiles) - 1):
        tiles.append(bytes(256))
    c1, c2 = encode_neogeo(tiles)
    open(oc1, 'wb').write(c1)
    open(oc2, 'wb').write(c2)
    print('tiles:', len(tiles), 'c1/c2 bytes:', len(c1))
    if '--pen0' in a:
        # one byte per arcade tile: bit 0 if it has at least 64 pen-0 pixels, bit 2 if it has any (the
        # runtime draws an opaque backdrop behind such layer-1 cells, since the arcade's back layer shows
        # palette colour 0 there: bit 0 in the races, where sprites per scanline are short, bit 2 elsewhere
        # - with 64 only, the crowd of the intro photos showed the sky through holes)
        # bit 1: every pixel is pen 0 (fully transparent tile: a layer column made only of these, or ending
        # with them, is shortened so that it does not count against the 96 sprites per scanline)
        def flag(t):
            n0 = sum(1 for p in t if p == 0)
            return (1 if n0 >= 64 else 0) | (2 if n0 == 256 else 0) | (4 if n0 else 0)
        flags = bytes(flag(t) for t in tiles[:16384])
        open(a[a.index('--pen0') + 1], 'wb').write(flags)
        print('pen0 flags:', sum(f & 1 for f in flags), 'empty tiles:', sum(1 for f in flags if f & 2))
    if '--png' in a:
        from PIL import Image
        cols = 128
        rows = (len(tiles) + cols - 1) // cols
        im = Image.new('P', (cols * 16, rows * 16))
        pal = []
        for i in range(16):
            pal += [i * 17, i * 17, i * 17]
        im.putpalette(pal + [0] * (768 - 48))
        px = im.load()
        for n, t in enumerate(tiles):
            tx, ty = (n % cols) * 16, (n // cols) * 16
            for y in range(16):
                for x in range(16):
                    px[tx + x, ty + y] = t[y * 16 + x]
        im.save(a[a.index('--png') + 1])


if __name__ == '__main__':
    main()
