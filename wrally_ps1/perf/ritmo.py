"""Ritmo de cada cuadro de juego (de work/perf/cuadros.csv): cuando se manda la lista a la GPU, cuando acaba de
dibujarla y cuando acaba la CPU (entrada al siguiente rc_vblank), relativos al vblank en que empezo el cuadro
(566000 ciclos = un periodo de video). Aparte, los cuadros sin lecturas de CD.
uso: perf/ritmo.py [dir_perfil] [desde] [hasta]   (por defecto work/perf 2000 2999)"""
import csv, sys, os
F = int(2152.0 * 263)
d = sys.argv[1] if len(sys.argv) > 1 else 'work/perf'
a = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
b = int(sys.argv[3]) if len(sys.argv) > 3 else 2999
rows = list(csv.DictReader(open(os.path.join(d, 'cuadros.csv'))))
cd = set()
for l in open(os.path.join(d, 'eventos.txt')):
    p = l.split()
    if len(p) > 3 and p[3] == 'CD_READ': cd.add(int(p[1]))
cpu, snd, gpu, fr = [], [], [], []
for i in range(max(a, 1), min(b, len(rows))):
    r = rows[i]
    if r['lista'] in ('', '0') or r['lista'] == rows[i - 1]['lista']: continue    # sin lista nueva (cuadro saltado)
    ls, le = int(r['lista']), int(r['gpu_fin'])
    v = ls // F
    cpu.append(int(r['ciclo']) - v * F); snd.append(ls - v * F); gpu.append(le - v * F); fr.append(i)
def pct(x, q): x = sorted(x); return x[min(len(x) - 1, int(q * len(x)))]
def show(nm, x):
    if not x: return
    print('%-16s p10 %7d  p50 %7d  p90 %7d  p99 %7d  max %8d  (p50 %3d%%, p90 %3d%%, p99 %3d%%)' % (nm, pct(x, .1), pct(x, .5), pct(x, .9), pct(x, .99), max(x), 100 * pct(x, .5) // F, 100 * pct(x, .9) // F, 100 * pct(x, .99) // F))
print('cuadros %d-%d (%d con lista nueva), ciclos desde el vblank en que empezo el cuadro:' % (a, b, len(fr)))
show('envio lista', snd); show('fin GPU', gpu); show('fin CPU', cpu)
ok = [k for k, i in enumerate(fr) if i not in cd]
show('fin CPU sin CD', [cpu[k] for k in ok])
show('CPU tras envio', [cpu[k] - snd[k] for k in ok])
vb = n = 0
for i in range(max(a, 1), min(b, len(rows))):
    if i in cd: continue
    vb += int(rows[i]['vblank']) - int(rows[i - 1]['vblank']); n += 1
print('cuadros de juego/s sin los cuadros con lectura de CD: %.1f' % (n / max(vb, 1) * 60))
