#!/usr/bin/env python3
"""Split a NeoSD .neo file into the ROM files referenced by neogeo.xml (softlist 'wrally')."""
import os, struct, sys

src, out = sys.argv[1], sys.argv[2]
d = open(src, "rb").read()
p, s, m, v1, v2, c = struct.unpack("<6I", d[4:28])
o = 4096
def take(n):
    global o
    r = d[o:o + n]; o += n
    return r
P, S, M, V, C = take(p), take(s), take(m), take(v1 + v2), take(c)
os.makedirs(out, exist_ok=True)
for name, blob in [("wr-p1.p1", P), ("wr-s1.s1", S), ("wr-m1.m1", M), ("wr-v1.v1", V),
                   ("wr-c1.c1", C[0::2]), ("wr-c2.c2", C[1::2])]:
    open(os.path.join(out, name), "wb").write(blob)
