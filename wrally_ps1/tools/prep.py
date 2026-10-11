import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from paths import rom, work
"""Prepara los datos a partir de tus ROM de MAME (wrally): programa del 68000, MCU y tiles"""
import numpy as np, shutil
a = open(rom('worldr16.c22'), 'rb').read(); b = open(rom('worldr17.c23'), 'rb').read()
p = bytearray(len(a) * 2); p[0::2] = b; p[1::2] = a
open(work('prog.bin'), 'wb').write(p)
shutil.copy(rom('wrdallas.bin'), work('mcu.bin'))
def ld16(even, odd):
    e = np.frombuffer(open(rom(even), 'rb').read(), np.uint8); o = np.frombuffer(open(rom(odd), 'rb').read(), np.uint8)
    r = np.empty(len(e) * 2, np.uint8); r[0::2] = e; r[1::2] = o; return r
reg = np.concatenate([ld16('worldr21.i13', 'worldr20.i11'), ld16('worldr19.i09', 'worldr18.i07')])
bits = np.unpackbits(reg); half = len(bits) // 2; n = half // 512
t = np.arange(n)[:, None, None] * 512; y = np.arange(16)[None, :, None] * 16
x = np.concatenate([np.arange(8), 256 + np.arange(8)])[None, None, :]
base = t + y + x
pix = (bits[base] | (bits[base + 8] << 1) | (bits[half + base] << 2) | (bits[half + base + 8] << 3)).astype(np.uint8)
np.save(work('tiles.npy'), pix)
print('prog.bin, mcu.bin, tiles.npy en', work(''))
