#!/usr/bin/env python3
"""Convert Thunder Hoop's graphics (c09..c12, 4 x 1 MB, MAME gaelco.cpp "gfx" region) into Neo Geo C ROMs.

Arcade region (4 MB): ROM_LOAD + ROM_CONTINUE put 256 KB quarters of each ROM at +0, +0x80000, +0x40000,
+0xC0000 of its megabyte (c12 -> 0 MB, c11 -> 1 MB, c10 -> 2 MB, c09 -> 3 MB).  Each megabyte is one bit
plane (MAME lists planes MSB first: 0 MB = pen bit 3).  8x8 tile n: byte n*8+y of a plane = row y, bit 7 = x 0.
A 16x16 tile m is the 8x8 tiles 4m (top left), 4m+1 (bottom left), 4m+2 (top right), 4m+3 (bottom right).
Sprites use 16x16 tiles 0-16383 (code >> 2) or single 8x8 tiles (code); the tilemaps use 16x16 tiles
16384-32767 (0x4000 + code).

Neo Geo C ROM tiles 0-32767 = the arcade 16x16 tiles, same numbers.  From 32768 on: every distinct
non-blank 8x8 sprite tile of the sprite half, padded into the top left quadrant of a 16x16 tile (the
runtime moves a flipped one by 8 pixels); map8.bin gives, for each of the 65536 8x8 codes, the Neo Geo
tile number minus 0x8000 (word, big endian; 0xFFFF = blank tile: the runtime hides the sprite).

The text font (8x8 sprite codes 0xF400-0xFFFF, always on the 8-pixel grid) also goes to the fix layer:
--fix in.s1 out.s1 copies the S ROM (the port's own font in tiles 0x000-0x3FF) and puts code 0xF000 + t in
fix tile t for t >= 0x400.

usage: gfx2crom.py <c09> <c10> <c11> <c12> <out_c1> <out_c2> <out_map8.bin> [--fix in.s1 out.s1]
                   [--png sheet.png first count]
"""
import sys


def load_region(c09, c10, c11, c12):
    reg = bytearray(0x400000)
    for base, path in ((0x300000, c09), (0x200000, c10), (0x100000, c11), (0x000000, c12)):
        d = open(path, 'rb').read()
        for q, off in enumerate((0, 0x80000, 0x40000, 0xC0000)):
            reg[base + off:base + off + 0x40000] = d[q * 0x40000:(q + 1) * 0x40000]
    return reg


def tile8(reg, n):
    """8x8 tile n -> 64 pens"""
    t = bytearray(64)
    for y in range(8):
        b3, b2, b1, b0 = (reg[p * 0x100000 + n * 8 + y] for p in range(4))
        for x in range(8):
            s = 7 - x
            t[y * 8 + x] = ((b3 >> s) & 1) << 3 | ((b2 >> s) & 1) << 2 | ((b1 >> s) & 1) << 1 | ((b0 >> s) & 1)
    return bytes(t)


def encode(t16):
    """256 pens (row major) -> (c1 64 bytes, c2 64 bytes) of one Neo Geo tile"""
    c1 = bytearray(64)
    c2 = bytearray(64)
    for y in range(16):
        for half, xb in ((0x40, 0), (0x00, 8)):
            p = [0, 0, 0, 0]
            for x in range(8):
                pen = t16[y * 16 + xb + x]
                for k in range(4):
                    p[k] |= ((pen >> k) & 1) << x
            o = (half + 4 * y) // 2
            c1[o], c2[o], c1[o + 1], c2[o + 1] = p[0], p[2], p[1], p[3]
    return bytes(c1), bytes(c2)


def join16(q):
    """4 8x8 tiles (TL, BL, TR, BR) -> 256 pens"""
    t = bytearray(256)
    for qi, (ox, oy) in enumerate(((0, 0), (0, 8), (8, 0), (8, 8))):
        for y in range(8):
            t[(oy + y) * 16 + ox:(oy + y) * 16 + ox + 8] = q[qi][y * 8:y * 8 + 8]
    return bytes(t)


def main(a):
    reg = load_region(*a[:4])
    oc1, oc2, omap = a[4:7]
    t8 = [tile8(reg, n) for n in range(0x20000)]
    c1, c2 = bytearray(), bytearray()
    for m in range(0x8000):
        x1, x2 = encode(join16(t8[4 * m:4 * m + 4]))
        c1 += x1
        c2 += x2
    blank = bytes(64)
    seen = {}
    m8 = bytearray(b'\xff' * 0x20000)
    nxt = 0x8000
    for n in range(0x10000):
        t = t8[n]
        if t == blank:
            continue
        if t not in seen:
            seen[t] = nxt
            x1, x2 = encode(join16((t, blank, blank, blank)))
            c1 += x1
            c2 += x2
            nxt += 1
        m8[2 * n:2 * n + 2] = (seen[t] - 0x8000).to_bytes(2, 'big')
    size = 1
    while size < len(c1):
        size *= 2
    c1 += bytes(size - len(c1))
    c2 += bytes(size - len(c2))
    open(oc1, 'wb').write(c1)
    open(oc2, 'wb').write(c2)
    open(omap, 'wb').write(m8)
    print('tiles 16x16: 32768, 8x8 sprite tiles: %d distinct (Neo Geo 0x8000-0x%04X), C ROM 2 x %d KB'
          % (nxt - 0x8000, nxt - 1, size // 1024))
    if '--fix' in a:
        i = a.index('--fix')
        srom = bytearray(open(a[i + 1], 'rb').read())
        for t in range(0x400, 0x1000):
            px = t8[0xF000 + t]
            for blk, x0 in enumerate((4, 6, 0, 2)):
                for y in range(8):
                    srom[t * 32 + blk * 8 + y] = px[y * 8 + x0] | px[y * 8 + x0 + 1] << 4
        open(a[i + 2], 'wb').write(srom)
    if '--png' in a:
        from PIL import Image
        i = a.index('--png')
        path, first, count = a[i + 1], int(a[i + 2], 0), int(a[i + 3], 0)
        im = Image.new('P', (32 * 16, (count + 31) // 32 * 16))
        im.putpalette([v for k in range(16) for v in (k * 17, k * 17, k * 17)])
        for k in range(count):
            m = first + k
            t = join16(t8[4 * m:4 * m + 4])
            for y in range(16):
                for x in range(16):
                    im.putpixel(((k % 32) * 16 + x, (k // 32) * 16 + y), t[y * 16 + x])
        im.save(path)


if __name__ == '__main__':
    main(sys.argv[1:])
