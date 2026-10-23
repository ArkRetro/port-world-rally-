#!/usr/bin/env python3
"""Tables for the 68000 Gaelco VRAM decryption of the port (src/crypt.s), Thunder Hoop parameters.

The arcade video hardware decrypts every word written to 0x100000-0x103FFF; the game writes almost only
pairs with move.l (first word: decrypt(0, 0, e1) = d1, second: decrypt(e1, d1, e2)).  The runtime does the
same in software with these tables (all words, big endian), written to <out.bin>:

  T0   [65536]  decrypt(0, 0, e)                                    (first word)
  TP   [65536]  per first encrypted word e1 (d1 = T0[e1]): bits 0-5 = k1 ^ P1, bits 6-10 = the part of k2
                that comes from e1/d1, ^ P1, bits 11-12 = swap, bits 13-14 = type
  BS   [4][2][256]  bitswap of e2 for each swap: [s][0][hi byte] ^ [s][1][lo byte] (P2 folded into the second)
  KR   [4][64]  the part of k2 that comes from (res & 0x3F) for each type (typ 0 also takes e2 bits 9 / 5)
  BF   [2][256] final bitswap: [0][hi byte] | [1][lo byte]
  PAL  [32768]  xBGR555 -> Neo Geo colour word

usage: mkcrypt.py <out.bin> [--check N]
"""
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis'))
from gaelcrypt import P1, P2, bit, bitswap16, decrypt  # noqa: E402

SW = [(1, 2, 0, 14, 12, 15, 4, 8, 13, 7, 3, 6, 11, 5, 10, 9), (14, 10, 4, 15, 1, 6, 12, 11, 8, 0, 9, 13, 7, 3, 5, 2),
      (2, 13, 15, 1, 12, 8, 14, 4, 6, 0, 9, 5, 10, 7, 3, 11), (3, 8, 1, 13, 14, 4, 15, 0, 10, 2, 7, 12, 6, 11, 9, 5)]
FIN = (2, 6, 0, 11, 14, 12, 7, 10, 5, 4, 8, 3, 9, 1, 13, 15)


def k1(typ, ep, dp):
    if typ == 0: return 0b111010
    if typ == 1: return bit(dp, 0) | bit(dp, 1) << 1 | bit(dp, 1) << 2 | bit(ep, 3) << 3 | bit(ep, 8) << 4 | bit(ep, 15) << 5
    if typ == 2: return bit(ep, 5) | bit(dp, 5) << 1 | bit(ep, 7) << 2 | bit(ep, 3) << 3 | bit(ep, 13) << 4 | bit(ep, 14) << 5
    return bit(ep, 0) | bit(ep, 9) << 1 | bit(ep, 6) << 2 | bit(dp, 4) << 3 | bit(ep, 2) << 4 | bit(dp, 11) << 5


def k2prev(typ, ep, dp):
    if typ == 0: return 0
    if typ == 1: return bit(dp, 2) | bit(ep, 4) << 1 | bit(dp, 14) << 2 | bit(dp, 12) << 4
    if typ == 2: return bit(ep, 6) | bit(dp, 6) << 1 | bit(dp, 15) << 2 | bit(dp, 7) << 4
    return bit(dp, 2) | bit(dp, 9) << 1 | bit(ep, 5) << 2 | bit(dp, 1) << 3 | bit(ep, 10) << 4


def kres(typ, r):
    if typ == 0: return bit(r, 2) << 1 | bit(r, 5) << 3 | bit(r, 4) << 4
    if typ == 1: return bit(r, 1) << 3
    if typ == 2: return bit(r, 0) << 3
    return 0


def tp(ep):
    dp = decrypt(0, 0, ep)
    swap = bit(dp, 8) << 1 | bit(dp, 7)
    typ = bit(dp, 12) << 1 | bit(dp, 2)
    return ((k1(typ, ep, dp) ^ P1) & 0x3F) | ((k2prev(typ, ep, dp) ^ P1) & 0x1F) << 6 | swap << 11 | typ << 13


def model(T0, TP, BS, KR, BF, e1, e2):
    """the algorithm of crypt.s, in Python"""
    d1 = T0[e1]
    t = TP[e1]
    s, typ = (t >> 11) & 3, (t >> 13) & 3
    res = BS[s][0][e2 >> 8] ^ BS[s][1][e2 & 255]
    res = (res & 0xFFC0) | ((res + (t & 0x3F)) & 0x3F)
    res ^= P1
    k = ((t >> 6) & 0x1F) ^ KR[typ][res & 0x3F]
    if typ == 0:
        k ^= bit(e2, 9) | bit(e2, 5) << 2
    res = (res & 0x3F) | ((res + (k << 6)) & 0x07C0) | ((res + (k << 11)) & 0xF800)
    res ^= ((P1 << 6) | (P1 << 11)) & 0xFFFF
    return d1, BF[0][res >> 8] | BF[1][res & 255]


def main(a):
    T0 = [decrypt(0, 0, e) for e in range(65536)]
    TP = [tp(e) for e in range(65536)]
    BS = [[[bitswap16(v << 8, *SW[s]) for v in range(256)], [bitswap16(v, *SW[s]) ^ P2 for v in range(256)]]
          for s in range(4)]
    KR = [[kres(t, r) for r in range(64)] for t in range(4)]
    BF = [[bitswap16(v << 8, *FIN) for v in range(256)], [bitswap16(v, *FIN) for v in range(256)]]
    n = int(a[a.index('--check') + 1]) if '--check' in a else 20000
    rnd = random.Random(1)
    for _ in range(n):
        e1, e2 = rnd.randrange(65536), rnd.randrange(65536)
        d1 = decrypt(0, 0, e1)
        assert model(T0, TP, BS, KR, BF, e1, e2) == (d1, decrypt(e1, d1, e2)), (e1, e2)

    def pal(v):
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        return (r & 1) << 14 | (g & 1) << 13 | (b & 1) << 12 | (r >> 1) << 8 | (g >> 1) << 4 | (b >> 1)
    out = bytearray()
    for w in T0 + TP + [x for s in BS for h in s for x in h] + [x for t in KR for x in t] + BF[0] + BF[1] + \
            [pal(v) for v in range(32768)]:
        out += w.to_bytes(2, 'big')
    open(a[0], 'wb').write(out)
    print('crypt tables: %d bytes, %d random pairs checked' % (len(out), n))


if __name__ == '__main__':
    main(sys.argv[1:])
