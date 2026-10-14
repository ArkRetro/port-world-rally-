"""sim.py --tramos carpeta [...] [--rot 300,416,600] [--margen 16] [--fijos 1024] [--subidas 40] [--pre 8] [--ade 24]
[--solo 4,8]: simulacion de la cache de huecos B en carrera (diseno 9.2, 10.6 y D9) con el mismo reparto que
paquetes.py: 4 352 huecos = fijos de partida (GAMEc) + VO del tramo (fijos durante el tramo, sin copia en la RAM) +
ROT rotativos, que al salir tienen los primeros de la L2 (los huecos del mapa se llenan en el negro).
Cada cuadro, los codigos visibles que no estan en la VRAM se suben desde la L2 si estan en ella (como mucho --subidas
por cuadro, contando la prelectura; los demas son falta ese cuadro). Victima (politica protectora): (a) hueco con
copia en la L2 muerto (FEFD08 > ultimo avance + margen), (b) el de copia en la L2 usado hace mas tiempo, (c) VO muerto;
nunca uno visible en el cuadro. Despues, prelectura de la L2 por primer avance (<= --pre por cuadro, primer avance <=
FEFD08 + --ade) solo en huecos libres o muertos, para no echar lo vivo.
Cuenta por linea (cada corrida que pasa por el tramo) con el paquete de todas ('dentro') y con el paquete de las demas
('fuera': la linea que se simula no esta en el paquete): cuadros con faltas (y cuantos tras la meta: FEC146 > 9),
rachas de > 3 cuadros seguidos del mismo codigo, atascos (no hay victima), VO perdidos (echado y visto despues),
subidas por cuadro (media, p99, max).
Puerta G-TILES: <= 0,1 % de cuadros con faltas y ninguna racha de > 3 fuera; 0 faltas dentro; 0 atascos."""
import sys, os, time
import numpy as np
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import paquetes

def victimas(rl2, rvo, uso, fin, a, j, vivos=True):
    """huecos que se pueden echar en el cuadro j con avance a, en orden: (a) L2 muertos, (b) L2 por LRU, (c) VO muertos"""
    c = np.fromiter(rl2, np.int64, len(rl2)); c = c[uso[c] != j]; m = fin[c] < a
    d = c[m]; yield from d[np.argsort(uso[d], kind='stable')].tolist()
    if vivos: d = c[~m]; yield from d[np.argsort(uso[d], kind='stable')].tolist()
    o = np.fromiter(rvo, np.int64, len(rvo)); o = o[(uso[o] != j) & (fin[o] < a)]
    yield from o[np.argsort(uso[o], kind='stable')].tolist()

def simula(t, fj, orden, nvo, pc, rot, margen, maxs=40, npre=8, ade=24):
    """t: un tramo de una corrida (paquetes.Corrida.tramos[k]); paquete: fijos fj, orden VO + L2, por codigo pc"""
    cls = np.zeros(65536, np.int8); fin = np.full(65536, 1 << 30, np.int64); ini = np.zeros(65536, np.int64)
    vo, l2 = orden[:nvo], orden[nvo:]; cls[list(fj)] = 1; cls[vo] = 2; cls[l2] = 3
    if orden:
        o = np.array(orden); fin[o] = [pc[x][4] + margen for x in orden]; ini[o] = [pc[x][3] for x in orden]
    res = np.zeros(65536, bool); uso = np.full(65536, -1, np.int64); echado = np.zeros(65536, bool)
    fr, fd, vis, off = t['fr'], t['fd08'], t['vis'], t['off']
    r = Counter(); subs = np.zeros(len(fr), np.int64); racha = {}; rachas = []; perdidos = set()
    def quita(x):
        res[x] = False
        if x in rl2: rl2.discard(x)
        else: rvo.discard(x); echado[x] = True; r['VO echados'] += 1
    for j in range(len(fr)):
        if j == 0 or fr[j] - fr[j - 1] > 10:        # salida (o vuelta a empezar): fijos, VO y ROT con el principio de la L2
            res[:] = False; uso[:] = -1; res[list(fj)] = True; res[vo] = True; res[l2[:rot]] = True
            rvo = set(vo); rl2 = set(l2[:rot]); libres = paquetes.HUECOS - len(fj) - len(vo) - min(rot, len(l2)); p = 0
        a = int(fd[j]); v = vis[off[j]:off[j + 1]]; uso[v] = j; up = 0; falta = []; vi = None
        for c in v[~res[v]].tolist():
            k = cls[c]
            if k != 3:
                falta.append(c); r['fuera del paquete' if k == 0 else 'VO perdido'] += 1
                if k == 2: perdidos.add(c)
                continue
            if up >= maxs: falta.append(c); r['tarde'] += 1; continue
            if libres: libres -= 1
            else:
                if vi is None: vi = victimas(rl2, rvo, uso, fin, a, j)
                x = next(vi, None)
                if x is None: falta.append(c); r['atascos'] += 1; continue
                quita(x)
            res[c] = True; rl2.add(c); up += 1
        vi = None; n = min(npre, maxs - up)          # prelectura: solo huecos libres o muertos
        while n > 0 and p < len(l2) and ini[l2[p]] <= a + ade:
            c = l2[p]
            if res[c] or fin[c] < a: p += 1; continue
            if libres: libres -= 1
            else:
                if vi is None: vi = victimas(rl2, rvo, uso, fin, a, j, False)
                x = next(vi, None)
                if x is None: break
                quita(x)
            res[c] = True; rl2.add(c); uso[c] = j; up += 1; n -= 1; p += 1; r['prelecturas'] += 1
        subs[j] = up
        if falta: r['cuadros con faltas'] += 1; r['faltas (tile.cuadro)'] += len(falta); r['tras la meta'] += t['f146'][j] > 9
        nuevo = {c: racha.get(c, 0) + 1 for c in falta}
        rachas += [x for c, x in racha.items() if c not in nuevo]; racha = nuevo
    rachas += list(racha.values())
    r['cuadros'] = len(fr); r['rachas > 3'] = sum(1 for x in rachas if x > 3); r['racha max'] = max(rachas, default=0)
    r['VO perdidos (tiles)'] = len(perdidos)
    r['subidas media'] = float(subs.mean()); r['subidas p99'] = float(np.percentile(subs, 99)); r['subidas max'] = int(subs.max())
    return r

def fila(nom, r):
    return ('  %-10s %6d %7.3f %% %5d %5d %4d %4d %4d %5d | %5.2f %5.0f %4d | %s' % (nom, r['cuadros'], 100.0 * r['cuadros con faltas'] / r['cuadros'],
            r['faltas (tile.cuadro)'], r['rachas > 3'], r['racha max'], r['atascos'], r['VO perdidos (tiles)'], r['tarde'],
            r['subidas media'], r['subidas p99'], r['subidas max'], ' '.join('%s %d' % (k, r[k]) for k in ('fuera del paquete', 'tras la meta', 'VO echados', 'prelecturas') if r[k])))

def main():
    a = sys.argv[1:]
    if '--tramos' not in a: print(__doc__); return
    def lista(op):
        if op not in a: return []
        i = a.index(op) + 1; r = []
        while i < len(a) and not a[i].startswith('-'): r.append(a[i]); i += 1
        return r
    def num(op, d): return int(a[a.index(op) + 1], 0) if op in a else d
    rots = [int(x) for x in (a[a.index('--rot') + 1] if '--rot' in a else '300,416,600').split(',')]
    margen = num('--margen', 16); maxs = num('--subidas', 40); npre = num('--pre', 8); ade = num('--ade', 24)
    cors = paquetes.lee_corridas(lista('--tramos')); t0 = time.time()
    fc, fj = paquetes.fijos_por_coche(cors, num('--fijos', 1024))
    ks = sorted({k for co in cors for k in co.tramos})
    if '--solo' in a: ks = [int(x) for x in a[a.index('--solo') + 1].split(',')]
    print('fijos %d; margen %d, subidas <= %d por cuadro, prelectura %d con primer avance <= FEFD08 + %d' % (len(fj), margen, maxs, npre, ade))
    tot = {}
    for k in ks:
        lin = paquetes.lineas_tramo(cors, k)
        todo = paquetes.reparto(lin, fj)                                         # paquete con todas las lineas
        fuera = [paquetes.reparto([x for x in lin if x is not l], fj) for l in lin]  # paquete sin la linea l
        for rot in rots:
            nvo = paquetes.HUECOS - len(fj) - rot
            print('\nST%02d ROT %d (VO %d, L2 %d con todas las lineas): %d lineas' % (k, rot, min(nvo, len(todo[1])), max(0, len(todo[1]) - nvo), len(lin)))
            print('  %-10s %6s %9s %5s %5s %4s %4s %4s %5s | %5s %5s %4s' % ('linea', 'cuad.', 'c. falta', 'tc', '>3', 'max', 'atas', 'VOp', 'tarde', 'sub/c', 'p99', 'max'))
            for (co, _), (pf, of, _) in zip(lin, fuera):
                t = co.tramos[k]; rd = simula(t, fj, todo[1], nvo, todo[0], rot, margen, maxs, npre, ade)
                rf = simula(t, fj, of, nvo, pf, rot, margen, maxs, npre, ade)
                print(fila(co.nom + ' dentro', rd)); print(fila(co.nom + ' fuera', rf))
                s = tot.setdefault((rot, k), Counter())
                s['lineas'] += 1; s['dentro faltas'] += rd['cuadros con faltas']; s['fuera cuadros'] += rf['cuadros']
                s['fuera faltas'] += rf['cuadros con faltas']; s['fuera rachas'] += rf['rachas > 3']; s['atascos'] += rd['atascos'] + rf['atascos']
                s['VO perdidos'] += rd['VO perdidos (tiles)'] + rf['VO perdidos (tiles)']; s['peor fuera'] = max(s['peor fuera'], 1e5 * rf['cuadros con faltas'] / rf['cuadros'])
                s['p99'] = max(s['p99'], rd['subidas p99']); s['max'] = max(s['max'], rd['subidas max'])
            print('  (%.0f s)' % (time.time() - t0), flush=True)
    print('\nResumen G-TILES (fuera: <= 0,1 %% de cuadros con faltas y 0 rachas > 3; dentro: 0 faltas; 0 atascos)')
    print('ROT tramo lineas | dentro: c.falta | fuera: c.falta (%%) peor linea (%%) rachas>3 | atascos VOp | p99 max | G-TILES')
    for (rot, k), s in sorted(tot.items()):
        pf = 100.0 * s['fuera faltas'] / s['fuera cuadros']; ok = s['dentro faltas'] == 0 and s['peor fuera'] / 1e3 <= 0.1 and not s['fuera rachas'] and not s['atascos']
        print('%3d ST%02d %3d | %6d | %6d (%6.3f) %7.3f %5d | %5d %4d | %3.0f %3d | %s'
              % (rot, k, s['lineas'], s['dentro faltas'], s['fuera faltas'], pf, s['peor fuera'] / 1e3, s['fuera rachas'], s['atascos'], s['VO perdidos'], s['p99'], s['max'], 'PASA' if ok else 'falla'))
    print('sim: %.0f s' % (time.time() - t0))

if __name__ == '__main__': main()
