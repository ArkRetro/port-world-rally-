"""Descubrimiento de codigo del 68000 por descenso recursivo (decodificador dec68 del port de WR1).
Semillas: los 64 vectores que apuntan a la ROM + tools/semillas.txt (tablas de saltos halladas con trazas de MAME).
Salida: work/code_addrs.txt (direcciones de instrucciones, lo que lee rc/rc68.py) y work/descent.txt (informe)"""
import sys, os, struct, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work, WR1
sys.path.insert(0, os.path.join(WR1, 'rc'))
from dec68 import Dec
D = open(work('prog.bin'), 'rb').read(); d = Dec(D)
seeds = set()
for v in range(64):
    x = struct.unpack('>I', D[v*4:v*4+4])[0] & 0xFFFFFF
    if 0x400 <= x < len(D) and not x & 1: seeds.add(x)
for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'semillas.txt')):
    l = l.split('#')[0].strip()
    if l: seeds.add(int(l, 16))
INS = {}; bad = {}; ind = []; work_ = sorted(seeds)
while work_:
    a = work_.pop()
    if a in INS or a in bad or a >= len(D) or a & 1: continue
    try: i = d.dec(a)
    except Exception as e: bad[a] = str(e); continue
    INS[a] = i; n = a + i.size; o = i.op
    if o == 'bra': work_.append(i.target)
    elif o in ('bcc', 'dbcc'): work_ += [n, i.target]
    elif o == 'bsr': work_ += [i.target, n]
    elif o in ('jsr', 'jmp'):
        if i.dst.m in ('absl', 'absw', 'pcd'): work_.append(i.dst.v)
        else: ind.append(a)
        if o == 'jsr': work_.append(n)
    elif o in ('rts', 'rte', 'rtr'): pass
    else: work_.append(n)
A = sorted(INS)
ov = sum(1 for a in A for k in range(a + 2, a + INS[a].size, 2) if k in INS)
ops = collections.Counter(INS[a].op for a in A)
open(work('code_addrs.txt'), 'w').write(''.join('%06x\n' % a for a in A))
with open(work('descent.txt'), 'w') as f:
    f.write('semillas %d, instrucciones %d, rango %06x-%06x, solapadas %d, no decodificables %d\n' % (len(seeds), len(A), A[0], A[-1], ov, len(bad)))
    for a in sorted(bad): f.write('  %06x %04x %s\n' % (a, struct.unpack('>H', D[a:a+2])[0], bad[a]))
    f.write('jsr/jmp indirectos %d\n' % len(ind))
    for a in ind: f.write('  %r\n' % INS[a])
    f.write('ops: ' + ' '.join('%s:%d' % kv for kv in sorted(ops.items(), key=lambda x: -x[1])) + '\n')
print(open(work('descent.txt')).readline().strip())
if bad or ov: sys.exit('descent: hay direcciones no decodificables o solapadas, ver work/descent.txt')
