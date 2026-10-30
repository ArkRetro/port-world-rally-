#!/usr/bin/env python3
"""Pack the staged ROM set (MAME on-disk convention, as written by mksoftlist.stage) into a NeoSD .neo
file, plus a zip of the MAME set.  The .neo layout follows NeoBuilder v1.06's generic conversion
(see ../../tools/builds/neobuilder_generic.py): 4096-byte header "NEO\\x01", region sizes, year, genre,
screenshot, NGH (from the P ROM header), name, manufacturer; then P, S, M, V, C.  P is the file as
stored in the MAME set (word-swapped), C is the byte interleave of c1/c2.

usage: mkneo.py <staged dir> <item> <out.neo> [-n Name] [-m Manufacturer] [-y Year] [-g Genre] [--zip out.zip]
"""
import argparse
import os
import struct
import zipfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('staged')
    ap.add_argument('item')
    ap.add_argument('out')
    ap.add_argument('-n', default='World Rally NG')
    ap.add_argument('-m', default='homebrew')
    ap.add_argument('-y', type=int, default=2026)
    ap.add_argument('-g', type=int, default=9, help='NeoSD genre (9 = driving)')
    ap.add_argument('-s', type=int, default=0, help='screenshot index')
    ap.add_argument('--zip', default=None, help='also write the MAME set as a zip')
    a = ap.parse_args()
    rd = lambda suffix: open(os.path.join(a.staged, f'{a.item}-{suffix}.{suffix}'), 'rb').read()
    p, s, m, v, c1, c2 = (rd(x) for x in ('p1', 's1', 'm1', 'v1', 'c1', 'c2'))
    c = bytearray(len(c1) + len(c2))
    c[0::2] = c1
    c[1::2] = c2
    for name, blob in (('P', p), ('S', s), ('M', m), ('V', v), ('C', c)):
        assert len(blob) % 16 == 0, name
    ngh = p[0x108] | (p[0x109] << 8)
    hdr = bytearray(4096)
    hdr[0:4] = b'NEO\x01'
    struct.pack_into('<6I', hdr, 4, len(p), len(s), len(m), len(v), 0, len(c))
    struct.pack_into('<4I', hdr, 0x1C, a.y, a.g, a.s, ngh)
    hdr[0x2C:0x2C + 32] = a.n.encode('latin-1')[:32].ljust(32, b'\0')
    hdr[0x4D:0x4D + 16] = a.m.encode('latin-1')[:16].ljust(16, b'\0')
    with open(a.out, 'wb') as f:
        f.write(hdr)
        for blob in (p, s, m, v, c):
            f.write(blob)
    print('wrote %s  P=%x S=%x M=%x V=%x C=%x NGH=%04X' % (a.out, len(p), len(s), len(m), len(v), len(c), ngh))
    if a.zip:
        with zipfile.ZipFile(a.zip, 'w', zipfile.ZIP_DEFLATED) as z:
            for suffix in ('p1', 's1', 'm1', 'v1', 'c1', 'c2'):
                fn = f'{a.item}-{suffix}.{suffix}'
                z.write(os.path.join(a.staged, fn), fn)
        print('wrote', a.zip)


if __name__ == '__main__':
    main()
