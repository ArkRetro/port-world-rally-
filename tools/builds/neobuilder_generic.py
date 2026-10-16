#!/usr/bin/env python3
"""Python replica of NeoBuilder v1.06's *generic zip* conversion
(NeoBuilder -n Name -m Manu -y Year -g Genre -s Screenshot file.zip), reconstructed from the
IL of NeoBuilder.exe (Program.CreateRomsetFromZip / CreateRomArea / CopyStream / BuildNeoFile).
Used to check that a .neo we produce is exactly what the official tool would build from the
same MAME-style files, since the tool itself only runs on Windows.

Rules reproduced:
  * file roles by name:  .+[-_][pP][12]\\.?.*  or .+\\.[pP][12]   (ordered by the P1/P2 token)
                         s1 / m1 / v[1234] / c[1-8] likewise; exactly one S1 and one M1 required.
  * P: p1,p2 concatenated as-is (LoadMode 0 = raw copy, no swapping).  A single P file whose
    middle begins with 10 00 00 F3 is treated as "reversed half P1" and its halves are swapped.
  * S, M: raw copy.  V: v1..v4 concatenated into V1, V2Size = 0.
  * C: pairs (c1,c2),(c3,c4)... byte-interleaved (LoadMode 1: c_odd at even bytes, c_even at odd).
  * area sizes rounded down to a multiple of 16; PSize capped at 9 MB.
  * header: "NEO" 0x01, sizes, Year, Genre, Screenshot, NGH = P[0x108] | P[0x109] << 8,
    Name (32 chars + NUL at 0x2C), Manufacturer (16 chars + NUL at 0x4D), rest zero, 4096 bytes.

usage: neobuilder_generic.py romset.zip out.neo -n Name -m Manu -y Year -g Genre [-s N]
"""
import argparse, re, struct, zipfile


def pick(names, pat1, pat2, token):
    sel = [n for n in names if re.match(pat1, n) or re.match(pat2, n)]
    key = lambda n: re.match(token, n).group(1).upper() if re.match(token, n) else n
    return sorted(sel, key=key)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('zip'); ap.add_argument('out')
    ap.add_argument('-n', required=True); ap.add_argument('-m', required=True)
    ap.add_argument('-y', type=int, required=True); ap.add_argument('-g', type=int, required=True)
    ap.add_argument('-s', type=int, default=0)
    a = ap.parse_args()
    z = zipfile.ZipFile(a.zip)
    names = [i.filename for i in z.infolist() if not i.is_dir()]
    P = pick(names, r'.+[-_][pP][12]\.?.*', r'.+\.[pP][12]', r'.+[-_\.]([pP][12]).*')
    S = pick(names, r'.+[-_][sS]1\.?.*', r'.+\.[sS]1', r'.+[-_\.]([sS]1).*')
    M = pick(names, r'.+[-_][mM]1\.?.*', r'.+\.[mM]1', r'.+[-_\.]([mM]1).*')
    V = pick(names, r'.+[-_][vV][1234]\.?.*', r'.+\.[vV][1234]', r'.+[-_\.]([vV][1234]).*')
    C = pick(names, r'.+[-_][cC][12345678]\.?.*', r'.+\.[cC][12345678]', r'.+[-_\.]([cC][12345678]).*')
    if not (P and len(S) == 1 and len(M) == 1 and V and C and len(C) % 2 == 0):
        raise SystemExit('Invalid romfile structure: P=%s S=%s M=%s V=%s C=%s' % (P, S, M, V, C))
    rd = lambda n: z.read(n)
    pdata = b''.join(rd(n) for n in P)
    if len(P) == 1:
        half = len(pdata) // 2
        if pdata[half:half + 4] == bytes([0x10, 0, 0, 0xF3]):
            print('Detected reversed half P1')
            pdata = pdata[half:] + pdata[:half]
    sdata, mdata = rd(S[0]), rd(M[0])
    vdata = b''.join(rd(n) for n in V)
    cdata = bytearray()
    for i in range(0, len(C), 2):
        c1, c2 = rd(C[i]), rd(C[i + 1])
        blk = bytearray(len(c1) + len(c2)); blk[0::2] = c1; blk[1::2] = c2
        cdata += blk
    al = lambda b: bytes(b[:len(b) & ~0xF])
    pdata, sdata, mdata, vdata, cdata = map(al, (pdata, sdata, mdata, vdata, cdata))
    psize = min(len(pdata), 9 * 1024 * 1024)
    ngh = pdata[0x108] | (pdata[0x109] << 8)
    hdr = bytearray(4096)
    hdr[0:4] = b'NEO\x01'
    struct.pack_into('<6I', hdr, 4, psize, len(sdata), len(mdata), len(vdata), 0, len(cdata))
    struct.pack_into('<4I', hdr, 0x1C, a.y, a.g, a.s, ngh)
    hdr[0x2C:0x2C + 32] = a.n.encode('latin-1')[:32].ljust(32, b'\0')
    hdr[0x4D:0x4D + 16] = a.m.encode('latin-1')[:16].ljust(16, b'\0')
    with open(a.out, 'wb') as f:
        f.write(hdr); f.write(pdata[:psize]); f.write(sdata); f.write(mdata); f.write(vdata); f.write(cdata)
    print('wrote', a.out, 'P=%x S=%x M=%x V=%x C=%x NGH=%04X' % (psize, len(sdata), len(mdata), len(vdata), len(cdata), ngh))


if __name__ == '__main__':
    main()
