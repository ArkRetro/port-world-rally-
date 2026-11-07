"""Tabla de duraciones (en cuadros) de cada frase del OKI M6295 segun el banco: oki_tab.c (uso personal)"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from paths import rom, work
oki = open(rom('worldr14.c01'), 'rb').read() + open(rom('worldr15.c03'), 'rb').read()
RATE = 1000000 / 132
def phrase(bank, p):
    e = oki[p * 8:p * 8 + 8]
    s = (e[0] << 16 | e[1] << 8 | e[2]) & 0x3ffff
    en = (e[3] << 16 | e[4] << 8 | e[5]) & 0x3ffff
    return s, en
out = ['/* generado por okitab.py */', 'const unsigned short oki_len[16][128] = {']
for b in range(16):
    row = []
    for p in range(128):
        s, en = phrase(b, p)
        row.append(0 if (p == 0 or en <= s) else int((en - s + 1) * 2 / RATE * 60) + 1)
    out.append('  {' + ','.join(map(str, row)) + '},')
out.append('};')
open('oki_tab.c', 'w').write('\n'.join(out) + '\n')
