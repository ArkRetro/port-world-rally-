"""Resumen de una medida de psxprof: cuadros de juego por segundo por tramos, vblanks por cuadro en carrera,
paradas largas y comparacion de la huella del estado del juego con perf/ref_estado.txt (si existe)."""
import csv, sys, os, collections, statistics
out, desde = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 2000
VB = 566000
rows = list(csv.DictReader(open(os.path.join(out, 'cuadros.csv'))))
def tramo(a, b, nombre):
    r = [x for x in rows if a <= int(x['cuadro_juego']) < b]
    if len(r) < 2: return
    vb = int(r[-1]['vblank']) - int(r[0]['vblank'])
    d = [int(x['ciclos_desde_anterior']) for x in r]
    c = collections.Counter(min(round(x / VB), 9) for x in d)
    print('%-26s %5.1f cuadros/s  (1 vbl: %3d%%, 2: %3d%%, >=3: %3d%%)  max %d vbl' % (
        nombre, len(r) / vb * 60, 100 * c[1] // len(r), 100 * c[2] // len(r), 100 * sum(v for k, v in c.items() if k >= 3) // len(r), max(d) // VB))
tramo(1, 600, 'arranque/attract (1-600)')
tramo(600, 1300, 'moneda/menu (600-1300)')
tramo(1300, 2000, 'carrera, inicio (1300-2000)')
tramo(2000, 10**9, 'carrera (2000-fin)')
ref = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ref_estado.txt')
if os.path.exists(ref):
    h = [l.strip() for l in open(ref) if l.strip()]
    mine = [x['estado'] for x in rows]
    n = min(len(h), len(mine))
    bad = [i for i in range(n) if h[i] != mine[i]]
    print('estado del juego vs referencia: %s (%d cuadros comparados%s)' % ('IDENTICO' if not bad else 'DISTINTO', n, '' if not bad else ', primer cuadro distinto %d' % bad[0]))
