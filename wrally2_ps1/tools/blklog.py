"""blklog.py fichero.blk [...]: resumen de los registros BLKLOG del banco (rc/blklog.c): por modo (FEC298), cuadros y
codigos distintos del plano visible, de sprites, de efectos y nuevos en todo el plano; por cuadro, mediana y maximo de
entradas visibles. Con -t, lo mismo por tramo (FEFD0A) en carrera y demo. Es la base de los paquetes (diseno 10)."""
import sys, struct
from collections import defaultdict

def lee(fn):
    """genera (cuadro, cabecera, entradas) por cuadro"""
    d = open(fn, 'rb').read(); p = 0; n = len(d) // 4
    w = struct.unpack('<%dI' % n, d[:n * 4])
    while p + 5 <= n:
        assert w[p] & 0x80000000, 'cabecera perdida en la palabra %d' % p
        fr, h = w[p] & 0x7FFFFFFF, w[p + 1:p + 5]; m = h[3] >> 16
        yield fr, h, w[p + 5:p + 5 + m]
        p += 5 + m

def clase(e): return (e >> 28) & 7
def codigo(e): return e & 0xFFFF
def color(e): return (e >> 16) & 0x7F

def main():
    fs = [a for a in sys.argv[1:] if not a.startswith('-')]
    portramo = '-t' in sys.argv
    modos = defaultdict(lambda: {'cuadros': 0, 'plano': set(), 'spr': set(), 'efe': set(), 'nuevo': set(), 'vis': []})
    for fn in fs:
        for fr, h, ent in lee(fn):
            modo = h[0] & 0xFF; tramo = h[2] & 0xFFFF
            k = (modo, tramo) if portramo and modo in (1, 5) else (modo, -1)
            s = modos[k]; s['cuadros'] += 1; vis = 0
            for e in ent:
                c = clase(e); key = (codigo(e), color(e))
                if c == 0: s['plano'].add(key); vis += 1
                elif c == 1: s['spr'].add(key); vis += 1
                elif c == 2: s['efe'].add(codigo(e)); vis += 1
                else: s['nuevo'].add(key)
            s['vis'].append(vis)
    print('%-16s %7s %8s %8s %6s %8s %12s' % ('modo/tramo', 'cuadros', 'plano', 'sprites', 'efecto', 'nuevos', 'visibles med/max'))
    for k in sorted(modos):
        s = modos[k]; v = sorted(s['vis'])
        nom = '%X' % k[0] + ('/%X' % k[1] if k[1] >= 0 else '')
        print('%-16s %7d %8d %8d %6d %8d %6d / %5d' % (nom, s['cuadros'], len(s['plano']), len(s['spr']), len(s['efe']), len(s['nuevo']), v[len(v) // 2], v[-1]))
    todo = set()
    for s in modos.values(): todo |= {c for c, _ in s['plano']} | {c for c, _ in s['spr']} | s['efe']
    print('codigos distintos vistos en total: %d' % len(todo))

main()
