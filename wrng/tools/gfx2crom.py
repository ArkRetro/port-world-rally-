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

Usage: gfx2crom.py <i13> <i11> <i9> <i7> <out_c1> <out_c2> [--png tiles.png]
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
    c1, c2 = encode_neogeo(tiles)
    open(oc1, 'wb').write(c1)
    open(oc2, 'wb').write(c2)
    print('tiles:', len(tiles), 'c1/c2 bytes:', len(c1))
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
