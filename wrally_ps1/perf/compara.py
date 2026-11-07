"""Informe de un escenario de psxprof (perf/escenarios.sh): ritmo, paradas, cargas, CD, tiles que faltan y, con la
linea base, imagenes distintas con el mismo estado del juego: si en el cuadro faltaban tiles (falta_capa/falta_spr
del dibujo que se ve) son tiles que faltan de verdad en pantalla; si no, otras diferencias de dibujo.
Los cuadros son los de cuadros.csv (la captura de ese momento es la del cuadro siguiente: SHOTAT=n+1).
uso: compara.py dir_nuevo [dir_base]"""
import csv, sys, os, collections
VB = 566000; CLK = 33868800.0
d = sys.argv[1]; base = sys.argv[2] if len(sys.argv) > 2 else None
rows = list(csv.DictReader(open(os.path.join(d, 'cuadros.csv'))))
N = len(rows)
fr = [int(r['cuadro_juego']) for r in rows]
vbl = [int(r['vblank']) for r in rows]
gap = [int(r['ciclos_desde_anterior']) for r in rows]
carga = [int(r.get('carga') or 0) for r in rows]
fl = [int(r.get('falta_capa') or 0) for r in rows]
fs = [int(r.get('falta_spr') or 0) for r in rows]
def secs(i, j): return (vbl[min(j, N - 1)] - vbl[i]) / 60.0   # de la fila i a la j
def runs(bad):
    """tramos [i, j) de filas seguidas con bad"""
    out, i = [], 0
    while i < N:
        if bad[i]:
            j = i
            while j < N and bad[j]: j += 1
            out.append((i, j)); i = j
        else: i += 1
    return out
def near_load(i, j): return any(carga[k] for k in range(max(0, i - 3), min(N, j + 3)))
# ritmo y paradas
print('arranque: %.2f s hasta el primer cuadro de juego' % (int(rows[1]['ciclo']) / CLK if N > 1 else 0))
loads = [(fr[i], gap[i] / CLK) for i in range(N) if carga[i] and fr[i] > 1]
print('cargas de tramo: %s' % (', '.join('cuadro %d %.2f s' % x for x in loads) or 'ninguna'))
tot = (vbl[-1] - vbl[1]) / 60.0 if N > 2 else 1
nl = [i for i in range(2, N) if not carga[i]]
vb_nl = sum(vbl[i] - vbl[i - 1] for i in nl)
st = [(fr[i], gap[i] / VB) for i in nl if gap[i] > 3.5 * VB]
print('cuadros de juego/s: %.1f en total, %.1f sin las cargas; paradas de mas de 3 vblanks fuera de las cargas: %d%s' % (
    (N - 2) / tot, len(nl) / max(vb_nl, 1) * 60, len(st), '' if not st else ' (max %.0f vbl en el cuadro %d; %s)' % (
        max(v for _, v in st), max(st, key=lambda x: x[1])[0], ', '.join('%d:%.0f' % x for x in st[:8]))))
c = collections.Counter(min(round(gap[i] / VB), 3) for i in nl)
print('cuadros fuera de las cargas que tardan 1 vblank: %d, 2: %d, 3 o mas: %d' % (c[1] + c[0], c[2], c[3]))
# CD
ev = collections.Counter(); lost_in = 0
evp = os.path.join(d, 'eventos.txt')
if os.path.exists(evp):
    for l in open(evp):
        p = l.split()
        if len(p) > 3: ev[p[3]] += 1
print('CD: %s' % ', '.join('%s %d' % (k, ev[k]) for k in ('CD_READ', 'CD_SECTOR_LOST', 'CD_PAUSE', 'CD_SPEED', 'CDDA_PLAY', 'CDDA_STOP', 'CDDA_END')))
# contadores del port: tiles visibles que no estaban en la cache al mandar la lista
def informe(nombre, bad, peso=None):
    rs = runs(bad)
    rs_nl = [(i, j) for i, j in rs if not near_load(i, j)]
    if not rs_nl: print('%s: ninguno' % nombre); return
    dur = sorted(((secs(i, j), i, j) for i, j in rs_nl), reverse=True)
    tf = sum(j - i for i, j in rs_nl)
    print('%s: %d cuadros en %d tramos, el mas largo %.2f s; tramos de mas de 0,5 s: %d; los mas largos: %s' % (
        nombre, tf, len(rs_nl), dur[0][0], sum(1 for x in dur if x[0] > 0.5),
        ', '.join('%d-%d %.2fs%s' % (fr[i], fr[j - 1], s, '' if not peso else ' (max %d)' % max(peso[i:j])) for s, i, j in dur[:6])))
informe('faltan tiles de las capas', [x > 0 for x in fl], fl)
informe('faltan 16 o mas de las capas', [x >= 16 for x in fl], fl)
informe('faltan sprites', [x > 0 for x in fs], fs)
# comparacion con la base
if base:
    br = list(csv.DictReader(open(os.path.join(base, 'cuadros.csv'))))
    bm = {int(r['cuadro_juego']): r for r in br}
    same = [None] * N; diff = [False] * N; nst = 0
    for i, r in enumerate(rows):
        b = bm.get(fr[i])
        if b is None: continue
        if b['estado'] != r['estado']: nst += 1; continue
        diff[i] = b.get('imagen') != r.get('imagen')
    print('base: %d cuadros con otro estado del juego%s' % (nst, '' if not nst else ' (no se comparan)'))
    # la imagen de la fila i es el dibujo de la fila i-1
    miss = [i > 0 and (fl[i - 1] > 0 or fs[i - 1] > 0) for i in range(N)]
    rs = runs(diff)
    tiles = [False] * N; other = [False] * N
    for i, j in rs:
        t = any(miss[i:j]) or any(miss[max(0, i - 1):i])
        for k in range(i, j): (tiles if t else other)[k] = True
    informe('imagen distinta de la base con tiles que faltan', tiles)
    informe('imagen distinta de la base sin tiles que falten (otras diferencias de dibujo)', other)
