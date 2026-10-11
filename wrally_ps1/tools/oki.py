import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from paths import rom, work
"""Frases del OKI M6295 y su banco (tabla del juego), y decodificador ADPCM"""
"""Convierte las muestras OKI M6295 de World Rally a ADPCM-A (YM2610) y genera tablas."""
import numpy as np, struct, subprocess, os
oki = open(rom('worldr14.c01'), 'rb').read() + open(rom('worldr15.c03'), 'rb').read()
STEPS = [16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]
def oki_decode(data):
    sig, si = -2, 0
    out = np.empty(len(data) * 2, np.int16)
    k = 0
    for b in data:
        for n in (b >> 4, b & 15):
            st = STEPS[si]
            d = ((2 * (n & 7) + 1) * st) // 8
            sig = sig - d if n & 8 else sig + d
            sig = max(-2048, min(2047, sig))
            si = max(0, min(48, si + IDX[n & 7]))
            out[k] = sig * 16; k += 1
    return out
# tabla de frases y banco de cada frase alta
phr = {}
bank = 2
prev_start = None
for p in range(1, 128):
    e = oki[p * 8:p * 8 + 8]
    s = (e[0] << 16 | e[1] << 8 | e[2]) & 0x3ffff
    en = (e[3] << 16 | e[4] << 8 | e[5]) & 0x3ffff
    if en <= s: continue
    if s >= 0x30000:
        if s == 0x30000: bank += 1
        # bancos segun la tabla de efectos del juego (grupos que empiezan en 0x30000)
        phr[p] = (s, en, bank)
    else:
        phr[p] = (s, en, None)
# ajustar bancos por la tabla del juego (pares banco,frase en 0x144ac)
D = open(work('prog.bin'), 'rb').read()
known = {}
for i in range(0x3c):
    b, f = D[0x144ac + 2 * i], D[0x144ac + 2 * i + 1]
    if b: known[f] = b & 15
# propagar: dentro de un grupo (misma secuencia desde 0x30000) el banco es el mismo
groups = []
for p in sorted(phr):
    s, en, b = phr[p]
    if b is None: continue
    if s == 0x30000 or not groups: groups.append([])
    groups[-1].append(p)
lastb = 2
for g in groups:
    bs = {known[p] for p in g if p in known}
    assert len(bs) <= 1, (g, bs)
    gb = bs.pop() if bs else lastb + 1
    lastb = gb
    for p in g:
        s, en, _ = phr[p]
        phr[p] = (s, en, gb)
