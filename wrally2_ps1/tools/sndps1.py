"""sndps1.py sndlog.txt eventos.txt [desfase (20)]: puerta G-SNDLOG en la PS1. Compara el SNDLOG del banco (rc_host:
"cuadro canal trozo t r1 r2 r3" y "cuadro canal para") con los sucesos de la SPU de psxprof2 (eventos.txt: SPU_KON y
SPU_SALTO). Comienzos (trozo con el canal parado) = KON de esa voz con la direccion de ese trozo (work/snd_tab.c) en
el mismo cuadro o el siguiente; cambios (trozo con el canal sonando) = salto por repeticion a ese trozo a +-30 cuadros.
Cuenta tambien los saltos del silencio a un trozo (hueco audible). desfase: cuadro del banco - cuadro de psxprof2 (el
banco cuenta los vblanks con la IRQ enmascarada del arranque). Sale con 1 si algo no casa."""
import re, sys, os
from collections import defaultdict, Counter
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work

SIL = 0x1000

def tabla():
    s = open(work('snd_tab.c')).read()
    ks = [int(x, 16) for x in re.search(r'snd_key\[SND_N\] = \{(.*?)\}', s).group(1).split(', ')]
    vs = [int(x, 16) for x in re.search(r'snd_val\[SND_N\] = \{(.*?)\}', s).group(1).split(', ')]
    d = defaultdict(set)
    for k, v in zip(ks, vs): d[v & 0xFFFFF].add(k)
    return d

def banco(fn):
    com, cam, act = [], [], defaultdict(bool)
    for l in open(fn):
        p = l.split(); fr, ch = int(p[0]), int(p[1])
        if p[2] == 'para': act[ch] = False; continue
        k = (int(p[4], 16) & 3) << 16 | (int(p[5], 16) - 0x1000) & 0xFFFF   # el registro lleva el final + 0x1000
        (cam if act[ch] else com).append((fr, ch, k)); act[ch] = True
    return com, cam

def main():
    a = sys.argv[1:]
    if len(a) < 2: print(__doc__); return
    des = int(a[2]) if len(a) > 2 else 20
    por_dir = tabla(); com, cam = banco(a[0]); kon, sal = [], []; hueco = 0
    for l in open(a[1]):
        p = l.split()
        if len(p) > 7 and p[3] == 'SPU_KON': kon.append((int(p[1]) + des, int(p[5]), int(p[7])))
        elif len(p) > 8 and p[3] == 'SPU_SALTO':
            x, y = int(p[6]), int(p[8])
            if y != SIL and x != y:
                sal.append((int(p[1]) + des, int(p[5]), y)); hueco += x == SIL
    fallos = 0
    us = set(); malos = []
    for fr, ch, k in com:
        m = [i for i, (f, v, d) in enumerate(kon) if i not in us and v == ch and 0 <= f - fr <= 1 and k in por_dir[d]]
        if m: us.add(m[0])
        else: malos.append((fr, ch, hex(k)))
    print('comienzos: banco %d, KON %d, casan %d; sin KON %d %s; KON de mas %d'
          % (len(com), len(kon), len(us), len(malos), malos[:8], len(kon) - len(us)))
    fallos += len(malos) + len(kon) - len(us)
    us = set(); malos = []; df = Counter()
    for fr, ch, k in cam:
        m = [(abs(f - fr), i) for i, (f, v, d) in enumerate(sal) if v == ch and i not in us and k in por_dir[d] and abs(f - fr) <= 30]
        if m: e, i = min(m); us.add(i); df[sal[i][0] - fr] += 1
        else: malos.append((fr, ch, hex(k)))
    otros = [sal[i] for i in range(len(sal)) if i not in us]
    print('cambios: banco %d, saltos a un trozo %d, casan %d (desfase en cuadros %s); sin salto %d %s; saltos de mas %d %s'
          % (len(cam), len(sal), len(us), sorted(df.items()), len(malos), malos[:8], len(otros), otros[:5]))
    print('saltos desde el silencio (hueco): %d' % hueco)
    fallos += len(malos) + len(otros) + hueco
    print('G-SNDLOG PS1: %s' % ('igual' if not fallos else 'FALLA (%d)' % fallos))
    sys.exit(1 if fallos else 0)

if __name__ == '__main__': main()
