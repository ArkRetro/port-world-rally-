"""Resumen de los escenarios de perf/escenarios.sh, una fila por escenario:
  faltan   segundos con tiles que faltan en pantalla (contadores del port) fuera de las cargas, en carrera y fuera
  peor     el tramo mas largo de cada (segundos y lo mas que faltaba a la vez: tiles de las capas + sprites)
  >0,5     tramos de mas de 0,5 s (en carrera, de mas de 0,35 s)
  base     con la linea base: segundos de imagen distinta con tiles que faltan (lo que se ve de verdad) y el peor
  cortes   frases de CD-DA cortadas y seguidas (cortes.py)
  paradas  cuadros de mas de 3 vblanks fuera de las cargas
  c/s car  cuadros de juego por segundo en carrera, sin las cargas
  cargas   tiempo de cada pantalla de carga; arranque: hasta el primer cuadro de juego
uso: tabla.py imagen.cue dir_escenarios esc1 [esc2 ...]"""
import csv, sys, os
VB = 566000; CLK = 33868800.0
cue, O, names = sys.argv[1], sys.argv[2], sys.argv[3:]
starts = []
for l in open(cue):
    p = l.split()
    if p[:2] == ['INDEX', '01']:
        m, s, f = map(int, p[2].split(':')); starts.append((m * 60 + s) * 75 + f)
def load(d):
    rows = list(csv.DictReader(open(os.path.join(d, 'cuadros.csv'))))
    g = lambda k: [int(r.get(k) or 0) for r in rows]
    return rows, g
def runs(bad):
    out, i, N = [], 0, len(bad)
    while i < N:
        if bad[i]:
            j = i
            while j < N and bad[j]: j += 1
            out.append((i, j)); i = j
        else: i += 1
    return out
def cortes(d):
    n, last = 0, None
    p = os.path.join(d, 'eventos.txt')
    if not os.path.exists(p): return 0
    for l in open(p):
        w = l.split()
        if len(w) < 6: continue
        if w[3] == 'CDDA_STOP': last = 1
        elif w[3] == 'CDDA_PLAY' and int(w[5]) not in starts and last: n += 1
    return n
hd = '%-8s %-25s %-25s %-12s %-18s %6s %7s %7s %s' % ('escena', 'faltan carrera: s, peor', 'faltan fuera: s, peor', '>0,35/>0,5', 'base: s, peor', 'cortes', 'paradas', 'c/s car', 'cargas / arranque')
print(hd); print('-' * len(hd))
for e in names:
    d = os.path.join(O, e)
    if not os.path.exists(os.path.join(d, 'cuadros.csv')): print('%-8s sin medida' % e); continue
    rows, g = load(d); N = len(rows)
    fr, vbl, gap, ca, fl, fs, rc = g('cuadro_juego'), g('vblank'), g('ciclos_desde_anterior'), g('carga'), g('falta_capa'), g('falta_spr'), g('carrera')
    near = lambda i, j: any(ca[max(0, i - 3):min(N, j + 3)])
    sec = lambda i, j: (vbl[min(j, N - 1)] - vbl[i]) / 60.0
    res = {}
    for k in (1, 0):
        rs = [(i, j) for i, j in runs([fl[i] + fs[i] > 0 and rc[i] == k for i in range(N)]) if not near(i, j)]
        lim = 0.35 if k else 0.5
        t = sum(sec(i, j) for i, j in rs)
        w = max(rs, key=lambda x: sec(*x)) if rs else None
        res[k] = ('%5.1f, %4.2f (%d)' % (t, sec(*w), max(fl[a] + fs[a] for a in range(*w)))) if w else '  0.0, -', sum(1 for x in rs if sec(*x) > lim)
    b = '-'
    bd = os.path.join(O, 'base_' + e)
    if os.path.exists(os.path.join(bd, 'cuadros.csv')):
        br = {int(r['cuadro_juego']): r for r in csv.DictReader(open(os.path.join(bd, 'cuadros.csv')))}
        diff = [False] * N; nst = 0
        for i, r in enumerate(rows):
            x = br.get(fr[i])
            if x is None: continue
            if x['estado'] != r['estado']: nst += 1; continue
            diff[i] = x.get('imagen') != r.get('imagen')
        miss = [i > 0 and (fl[i - 1] + fs[i - 1] > 0) for i in range(N)]
        rs = [(i, j) for i, j in runs(diff) if (any(miss[i:j]) or any(miss[max(0, i - 1):i])) and not near(i, j)]
        w = max(rs, key=lambda x: sec(*x)) if rs else None
        b = '%5.1f, %4.2f' % (sum(sec(i, j) for i, j in rs), sec(*w) if w else 0) + ('' if not nst else ' (estado %d)' % nst)
    st = sum(1 for i in range(2, N) if not ca[i] and gap[i] > 3.5 * VB)
    ri = [i for i in range(2, N) if rc[i] and not ca[i]]
    fps = '%.1f' % (len(ri) / max(1, sum(vbl[i] - vbl[i - 1] for i in ri)) * 60) if ri else '-'
    loads = ' '.join('%.2f' % (gap[i] / CLK) for i in range(N) if ca[i] and fr[i] > 1)
    boot = int(rows[1]['ciclo']) / CLK if N > 1 else 0
    print('%-8s %-25s %-25s %-12s %-18s %6d %7d %7s %s / %.2f' % (e, res[1][0], res[0][0], '%d/%d' % (res[1][1], res[0][1]), b, cortes(d), st, fps, loads or '-', boot))
