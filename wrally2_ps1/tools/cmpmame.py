"""Compara los volcados del banco de pruebas (rc_host DUMP=..., <carpeta>/dumps.bin) con los de MAME (lua de referencia
del analisis: d<cuadro>.bin o dumps.bin, formato WR2D). Por cada cuadro comun: bytes distintos por region y los primeros
tramos. Se ignoran la estadistica de la espera de cada cuadro (FE8118-FE8123), el contador vivo de la MCU (FEFC10) y
los registros del GAE1 (VRAM 0x2890-0x28FF).
Uso: cmpmame.py banco/dumps.bin carpeta_mame|mame/dumps.bin [regiones] [-n tramos] [-q]"""
import sys, os, struct, glob

def lee(path):
    d = open(path, 'rb').read(); p = 0; out = {}
    while p < len(d):
        assert d[p:p + 4] == b'WR2D', path
        _, fr, n, _ = struct.unpack('>IIId', d[p + 4:p + 24]); p += 24; bl = {}
        for _ in range(n):
            base, ln = struct.unpack('>II', d[p:p + 8]); p += 8; bl[base] = d[p:p + ln]; p += ln
        out[fr] = bl
    return out

def lee_mame(arg):
    if os.path.isdir(arg):
        out = {}
        for f in sorted(glob.glob(os.path.join(arg, 'd*.bin'))): out.update(lee(f))
        return out
    return lee(arg)

REG = {'ram': (0xFE0000, 0x10000), 'vram': (0x200000, 0x10000), 'pal': (0x210000, 0x2000), 'vregs': (0x218004, 6)}
IGN = [(0xFE8118, 0xFE8124), (0xFEFC10, 0xFEFC12), (0x202890, 0x202900)]

def tramos(a, b, base):
    r = []; i = 0; n = len(a)
    while i < n:
        if a[i] != b[i] and not any(x <= base + i < y for x, y in IGN):
            j = i
            while j < n and (a[j] != b[j] or any(x <= base + j < y for x, y in IGN)): j += 1
            r.append((base + i, j - i)); i = j
        else: i += 1
    return r

def main():
    a = [x for x in sys.argv[1:] if not x.startswith('-')]
    nt = int(sys.argv[sys.argv.index('-n') + 1]) if '-n' in sys.argv else 4
    q = '-q' in sys.argv
    regs = [x for x in a[2:] if x in REG] or list(REG)
    A = lee(a[0]); B = lee_mame(a[1])
    comunes = sorted(set(A) & set(B)); iguales = 0; primero = None
    for fr in comunes:
        msg = []
        for r in regs:
            base, n = REG[r]
            x, y = A[fr].get(base), B[fr].get(base)
            if x is None or y is None: continue
            t = tramos(x[:n], y[:n], base)
            if t:
                msg.append('%s %d bytes: %s' % (r, sum(l for _, l in t), ' '.join('%06X+%d' % s for s in t[:nt])))
        if msg:
            if primero is None: primero = fr
            if not q or fr == primero: print('cuadro %d: %s' % (fr, ' | '.join(msg)))
        else: iguales += 1
    print('%d cuadros comunes, %d iguales%s' % (len(comunes), iguales, ', primero distinto %d' % primero if primero else ''))

if __name__ == '__main__': main()
