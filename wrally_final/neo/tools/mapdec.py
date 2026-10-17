"""Descifrado (vectorizado) de pares de palabras para la VRAM de Gaelco, igual que gdec2 del port."""
import numpy as np
B = '/home/claude/wrally/neo/build/'
def _be(fn): return np.frombuffer(open(B + fn, 'rb').read(), dtype='>u2').astype(np.int64)
SW = _be('sw.bin').reshape(4, 2, 256); FIN = _be('fin.bin').reshape(2, 256)
FIRST = _be('first.bin'); GI = _be('gdinfo.bin')
def dec_pairs(e0, e1):
    e0 = np.asarray(e0, np.int64); e1 = np.asarray(e1, np.int64)
    inf = GI[e0]; sw = inf & 3
    res = (SW[sw, 0, e1 >> 8] | SW[sw, 1, e1 & 0xff]) ^ 0x522a
    res = (res & 0xffc0) | ((res + ((inf >> 4) & 0x3f)) & 0x3f)
    res ^= 0x1f
    ty = (inf >> 2) & 3
    k = inf >> 10
    k0 = ((e1 >> 9) & 1) | ((res >> 1) & 2) | ((e1 >> 3) & 4) | ((res >> 2) & 8) | (res & 0x10)
    k = np.where(ty == 0, k0, k)
    k = np.where(ty == 1, k | ((res << 2) & 8), k)
    k = np.where(ty == 2, k | ((res << 3) & 8), k)
    k ^= 0x1f
    res = (res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800)
    res = (res ^ 0xffc0) & 0xffff
    d1 = FIN[0, res >> 8] | FIN[1, res & 0xff]
    return FIRST[e0], d1
