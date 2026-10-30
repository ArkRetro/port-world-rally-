"""Convierte las muestras OKI M6295 de World Rally a ADPCM-A (YM2610) y genera tablas."""
import numpy as np, struct, subprocess, os
R = '/home/claude/wrally/rom/'
OUT = '/home/claude/wrally/neo/build/'
oki = open(R + 'worldr14.c01', 'rb').read() + open(R + 'worldr15.c03', 'rb').read()
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
D = open('/home/claude/wrally/prog.bin', 'rb').read()
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
RATE_IN, RATE_OUT = 1000000 / 132, 8000000 / 432
from scipy.signal import resample_poly
from fractions import Fraction
fr = Fraction(RATE_OUT / RATE_IN).limit_denominator(200)
vrom = bytearray()
ztab = bytearray(128 * 4)
dur = [0] * 128
info = []
for p in sorted(phr):
    s, en, b = phr[p]
    if s >= 0x30000:
        if b is None: continue
        base = b * 0x10000 - 0x30000
    else:
        base = 0
    data = oki[base + s:base + en + 1]
    pcm = oki_decode(data).astype(np.float64)
    pcm = resample_poly(pcm, fr.numerator, fr.denominator)
    pcm = 32767 * np.tanh(pcm * 1.5 / 32767)          # +3,5 dB con limitador suave
    pcm = np.clip(pcm, -32768, 32767).astype('<i2')
    pcm.tofile('/tmp/claude-0/s.raw')
    subprocess.check_call(['./adpcma_enc', '/tmp/claude-0/s.raw', '/tmp/claude-0/s.bin'])
    a = open('/tmp/claude-0/s.bin', 'rb').read()
    a += b'\x80' * ((-len(a)) % 256)          # relleno hasta bloque de 256
    st = len(vrom)
    if (st >> 20) != ((st + len(a) - 1) >> 20):     # no cruzar limite de 1 MB
        vrom += b'\x80' * ((1 << 20) - (st & 0xFFFFF)); st = len(vrom)
    vrom += a
    sb, eb = st >> 8, (st + len(a) - 1) >> 8
    struct.pack_into('<HH', ztab, p * 4, sb, eb)
    dur[p] = int(len(data) * 2 / RATE_IN * 59.185) + 2
    info.append((p, b, len(data), len(a)))
while len(vrom) & 0xFFFFF: vrom += b'\x80'
open(OUT + 'v1.bin', 'wb').write(vrom)
open(OUT + 'snd_ztab.bin', 'wb').write(ztab)
open(OUT + 'snd_dur.bin', 'wb').write(struct.pack('>128H', *dur))
print('muestras', len(info), 'V-ROM', len(vrom), 'sin banco:', [p for p in phr if phr[p][2] is None and phr[p][0] >= 0x30000])
