"""paquetes.py [--blk blk.bin ... --dumps dumps.bin ... --romstat romstat.txt ...] [--tramos carpeta ...] [-o work/cd]
[--margen 16] [--vo 4352] [--rot 416] [--fijos 1024] [--romres work/romres.txt] [--area 1005568]: los paquetes de video
del CD (formatos.md 3 y 5, diseno 2.3 y 10) desde las corridas del banco.
Attract (--blk, --dumps, --romstat):
- MENUS.PAK: una seccion A (tipo 0) por pantalla (clave modo << 16 | FEC146; todo lo visible: plano y sprites) de los
  modos que no son carrera (1), demo (5), mapa de demo (9) ni negro (0). Avisa si una pantalla pasa de 2 560 tiles.
- DEMOr.PAK (r = FEC3C6 de la demo): secciones mapa (1, tipo 3: lo visto en modo 9 con FEC146 0x19 + r), VO (2, tipo 4:
  los primeros tiles B de la demo por primer cuadro de uso hasta llenar los huecos), L2 (3, tipo 1: el resto y, al
  final, los codigos escritos en el plano que nunca se vieron) y ROM (4, tipo 2: el banco del tramo (r, 0) de ROMSTAT).
  Registros B con la paleta mas vista en esa pantalla y, en los bytes 142-143, el ultimo avance FEFD08 + margen.
Las corridas son deterministas desde el encendido: se dan los BLKLOG, volcados y ROMSTAT de la corrida larga del
attract y de la partida (lo que sigue a la partida son ciclos con otra semilla).
Carrera (--tramos: carpetas de work/tramos con blk.bin y dumps.bin; cada una, una partida con el piloto):
- GAMEc.PAK (c = coche por FE8124 de los volcados: N, L, R): los --fijos tiles B de partida (VO, tipo 4, avance 0xFFFF):
  los codigos vistos en carrera (modo 1) en mas tramos y mas cuadros, de los vistos en >= 10 de los 12.
- STkk.PAK (kk = FEFD0A 0-11): mapa (1, tipo 3: modo B con la clave FEC3C6/FEC3C8 del tramo), VO (2, tipo 4: por primer
  avance FEFD08 hasta 4 352 - fijos - --rot), L2 (3, tipo 1: el resto y el cierre: codigos del plano nunca visibles) y
  ROM (4, tipo 2: el banco de romres.txt). Union de las corridas que pasan por el tramo, sin los codigos fijos.
  Cifras por tramo y puerta G-RAM (area >= max(L2 + banco) x 1,02)."""
import sys, os, struct, time
import numpy as np
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tiles4, tilesbin, romstat, rombank

WORK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work')
MAX_A = 2560                      # huecos A de la VRAM (10 paginas)
MAPA_146 = 0x19                   # FEC146 del mapa de la demo del rally 0

def lee_blk(fn):
    """BLKLOG -> (por cuadro: cuadro, modo, fec146, fec3c6, fefd08, n), entradas (u32)"""
    w = np.fromfile(fn, dtype='<u4'); p = 0; cab = []; ent = []
    while p + 5 <= len(w):
        n = int(w[p + 4] >> 16); h = w[p:p + 5]
        cab.append((int(h[0] & 0x7FFFFFFF), int(h[1] & 0xFF), int(h[1] >> 16), int(h[2] & 0xFFFF), int(h[3] >> 16), n))
        ent.append(w[p + 5:p + 5 + n]); p += 5 + n
    return cab, ent

def clave_pantalla(modo, f146):
    """a que paquete/seccion va un cuadro: ('menu', clave), ('mapa', r), ('demo', r) o None"""
    if modo == 5: return None                       # la demo va por FEC3C6 (abajo)
    if modo == 9: return ('mapa', f146 - MAPA_146) if 0 <= f146 - MAPA_146 < 4 else None
    if modo in (0, 1): return None
    return ('menu', modo << 16 | f146)

class Vistos:
    """por pantalla: instancias [(a, b)] y, por (codigo, color, clase): cuenta, primer cuadro relativo, primer y ultimo
    avance; con numpy porque son decenas de millones de entradas"""
    def __init__(self):
        self.inst = defaultdict(list); self.datos = defaultdict(list)
    def suma(self, fn, corrida=0):
        """corrida: indice del fichero (sus cuadros van desplazados como en tiles4.Paletas)"""
        cab, ent = lee_blk(fn); prev = None; base = corrida * tiles4.CORRIDA
        for (fr, modo, f146, c6, fd08, n), e in zip(cab, ent):
            fr += base
            k = ('demo', c6) if modo == 5 else clave_pantalla(modo, f146)
            if k != prev:
                if prev is not None: self.inst[prev][-1][1] = ultimo
                if k is not None: self.inst[k].append([fr, fr])
                prev = k; ini = fr
            ultimo = fr
            if k is None or not n: continue
            self.datos[k].append((e, fr, fr - ini, fd08))
        if prev is not None: self.inst[prev][-1][1] = ultimo
    def resumen(self, k):
        """(codigo, color, clase) -> [cuenta, primer cuadro relativo, primer avance, ultimo avance]"""
        d = self.datos[k]
        return resume(np.concatenate([x[0] for x in d]), np.concatenate([np.full(len(x[0]), x[2]) for x in d]),
                      np.concatenate([np.full(len(x[0]), x[3]) for x in d]))

def resume(e, rel, av):
    """entradas BLKLOG con su cuadro relativo y su avance -> (codigo, color, clase) -> [cuenta, primer cuadro relativo,
    primer avance, ultimo avance]"""
    key = (e & 0xFFFF).astype(np.int64) << 16 | ((e >> 28) & 3) << 8 | (e >> 16) & 0x7F
    u, inv, cn = np.unique(key, return_inverse=True, return_counts=True)
    r0 = np.full(len(u), 1 << 30); a0 = np.full(len(u), 1 << 30); a1 = np.full(len(u), -1)
    np.minimum.at(r0, inv, rel); np.minimum.at(a0, inv, av); np.maximum.at(a1, inv, av)
    return {(k >> 16, k & 0x7F, (k >> 8) & 3): [c, r, p, q] for k, c, r, p, q in zip(u.tolist(), cn.tolist(), r0.tolist(), a0.tolist(), a1.tolist())}

def junta(ds):
    """union de resumenes de varias lineas: cuentas sumadas, minimo del primer cuadro y del primer avance, maximo del
    ultimo avance (FEFD08 no es monotono y cada linea va a su paso)"""
    out = {}
    for d in ds:
        for k, (c, r, p, q) in d.items():
            o = out.get(k)
            out[k] = [c, r, p, q] if o is None else [o[0] + c, min(o[1], r), min(o[2], p), max(o[3], q)]
    return out

def por_codigo(res):
    """resumen por (codigo, color, clase) -> codigo -> (color mas visto, banderas, primer cuadro relativo, primer avance,
    ultimo avance, nunca visible: solo escrito en el plano). Sprite = nunca en el plano; efecto = solo como efecto"""
    out = {}
    for (t, col, cl), (c, r, p, q) in res.items():
        o = out.setdefault(t, [Counter(), Counter(), set(), 1 << 30, 1 << 30, -1])
        (o[1] if cl == 3 else o[0])[col] += c; o[2].add(cl)
        o[3] = min(o[3], r); o[4] = min(o[4], p); o[5] = max(o[5], q)
    for t, (vis, nue, cls, r, p, q) in out.items():
        fl = (tiles4.SPRITE if 0 not in cls and 3 not in cls else 0) | (tiles4.EFECTO if cls == {2} else 0)
        out[t] = ((vis or nue).most_common(1)[0][0], fl, r, p, q, not vis)
    return out

def indice(entradas, paquete):
    """sector(es) de indice (tipo 5): entradas (clave, sector, sectores, registros) de 16 B, 125 por sector, con
    terminador 0xFFFFFFFF; las entradas se refieren al sector absoluto del fichero. El byte 5 cuenta las entradas"""
    e = list(entradas) + [(0xFFFFFFFF, 0, 0, 0)]; out = []
    for i in range(0, len(e), 125):
        s = bytearray(tilesbin.sector(5, [b''.join(struct.pack('<IIII', *x) for x in e[i:i + 125])], paquete, i // 125))
        s[5] = sum(1 for x in e[i:i + 125] if x[0] != 0xFFFFFFFF); out.append(bytes(s))
    return out

def n_idx(nsec): return (nsec + 1 + 124) // 125

def sectores_b(regs, tipo, paquete, idx0, avances):
    """registros B de 144 bytes en sectores de 14; bytes 12-15 = menor primer avance del sector"""
    out = []
    for i in range(0, len(regs), 14):
        out.append(tilesbin.sector(tipo, regs[i:i + 14], paquete, idx0 + len(out), min(avances[i:i + 14]) if avances else 0))
    return out

def escribe_pak(fn, paquete, secciones):
    """secciones [(clave, [sectores], registros)] -> fichero con indice al principio; devuelve las entradas del indice"""
    ni = n_idx(len(secciones)); ent = []; sec = ni
    for clave, ss, nr in secciones:
        ent.append((clave, sec, len(ss), nr)); sec += len(ss)
    cuerpo = b''.join(b''.join(ss) for _, ss, _ in secciones)
    idx = b''.join(indice(ent, paquete)); assert len(idx) == ni * 2048
    with open(fn, 'wb') as f: f.write(idx + cuerpo)
    return ent

COCHES = {0x0000: 'N', 0x04B0: 'L', 0x0320: 'R'}   # FE8124 -> coche; GAMEc.PAK es el paquete 0x0400 + 'NLR'.index(c)
HUECOS = 4352                                       # huecos B en carrera (b0-b16)

def rangos(st, ln):
    """indices de las entradas de varios cuadros (inicio, largo) en un solo array"""
    ln = np.asarray(ln, np.int64)
    return np.repeat(np.asarray(st, np.int64) - np.cumsum(ln) + ln, ln) + np.arange(int(ln.sum()))

class Corrida:
    """una partida del banco (carpeta de work/tramos): por tramo FEFD0A (modo 1) la clave (FEC3C6, FEC3C8), los cuadros,
    FEFD08, FEC146, el resumen por (codigo, color, clase) y los codigos visibles de cada cuadro (para sim.py); por clave, el
    resumen del mapa (modo B); las paletas y el coche (FE8124) de los volcados"""
    def __init__(self, d):
        self.d = d; self.nom = os.path.basename(os.path.normpath(d))
        w = np.fromfile(os.path.join(d, 'blk.bin'), dtype='<u4'); p = 0; P = []
        while p + 5 <= len(w): P.append(p); p += 5 + int(w[p + 4] >> 16)
        P = np.array(P, np.int64); H = w[P[:, None] + np.arange(5)].astype(np.int64); P += 5; N = H[:, 4] >> 16
        fr = H[:, 0] & 0x7FFFFFFF; modo = H[:, 1] & 0xFF; c68 = H[:, 2]; fd0a = H[:, 3] & 0xFFFF; fd08 = H[:, 3] >> 16
        self.tramos = {}; self.mapas = {}
        for k in np.unique(fd0a[modo == 1]).tolist():
            i = np.flatnonzero((modo == 1) & (fd0a == k)); e = w[rangos(P[i], N[i])]; f = np.repeat(np.arange(len(i)), N[i])
            cl = Counter(c68[i].tolist()).most_common(1)[0][0]; m = (e >> 28) < 3
            u = np.sort(f[m].astype(np.int64) << 16 | e[m] & 0xFFFF); u = u[np.r_[True, u[1:] != u[:-1]]]  # visibles sin repetir
            self.tramos[k] = dict(clave=(cl & 0xFFFF, cl >> 16), fr=fr[i], fd08=fd08[i], f146=H[i, 1] >> 16, iv=[(int(fr[i[0]]), int(fr[i[-1]]))],
                                  res=resume(e, fr[i][f] - fr[i[0]], fd08[i][f]), vis=(u & 0xFFFF).astype(np.int64),
                                  off=np.searchsorted(u >> 16, np.arange(len(i) + 1)))
        for cl in np.unique(c68[modo == 0xB]).tolist():
            i = np.flatnonzero((modo == 0xB) & (c68 == cl)); e = w[rangos(P[i], N[i])]; z = np.zeros(len(e), np.int64)
            self.mapas[(cl & 0xFFFF, cl >> 16)] = dict(res=resume(e, z, z), iv=[(int(fr[i[0]]), int(fr[i[-1]]))])
        self.pal = tiles4.Paletas([os.path.join(d, 'dumps.bin')])
        cs = Counter(v for x, v in self.pal.coche.items() if any(a <= x <= b for t in self.tramos.values() for a, b in t['iv']))
        self.coche = COCHES.get(cs.most_common(1)[0][0], '%04X' % cs.most_common(1)[0][0]) if cs else '?'

def lee_corridas(ds):
    t0 = time.time(); cors = [Corrida(d) for d in ds]
    print('corridas: %d en %.1f s; coche %s' % (len(cors), time.time() - t0, ' '.join('%s=%s' % (co.nom, co.coche) for co in cors)))
    return cors

def paletas_estados(ivs, c, n=32):
    """los estados de la paleta c entre varias corridas (tiles4.Paletas.estados), como mucho n"""
    from collections import Counter
    cnt = Counter()
    for co, iv in ivs:
        for p in co.pal.estados(c, iv, n): cnt[tuple(p)] += 1
    return [list(k) for k, _ in cnt.most_common(n)]

def paleta_viva(ivs, c):
    """los 32 colores de la paleta c del volcado mas vivo (los fundidos la apagan) entre varias corridas:
    ivs = [(corrida, [(a, b)])]"""
    mejor = None
    for co, iv in ivs:
        fr = co.pal.elige(c, iv); br = co.pal.brillo[np.searchsorted(co.pal.cuadros, fr), c]
        if mejor is None or br > mejor[0]: mejor = (br, co, fr)
    return mejor[1].pal.paleta(mejor[2], c)

def fijos(cors, n=1024, minimo=10):
    """fijos de partida de un coche (diseno 2.3, 4.1, 9.1: poses, marcador, efectos comunes) desde sus corridas: los
    codigos vistos en carrera en >= minimo tramos, por tramos en que se ven y por cuadros; los n primeros"""
    nt = Counter(); fq = Counter(); ks = sorted({k for co in cors for k in co.tramos})
    for k in ks:
        vis = Counter()
        for (t, col, cl), (c, _, _, _) in junta([co.tramos[k]['res'] for co in cors if k in co.tramos]).items():
            if cl < 3: vis[t] += c
        for t, c in vis.items(): nt[t] += 1; fq[t] += c
    cand = sorted((t for t in nt if nt[t] >= min(minimo, len(ks))), key=lambda t: (-nt[t], -fq[t], t))
    return cand[:n], len(cand)

def vistos_carrera(cors, ks, clases=(0, 1, 2)):
    """codigo -> cuadros vistos en carrera en los tramos ks de esas corridas (clases: 0 plano, 1 sprite, 2 efecto)"""
    v = Counter()
    for co in cors:
        for k in ks:
            if k in co.tramos:
                for (t, col, cl), (c, _, _, _) in co.tramos[k]['res'].items():
                    if cl in clases: v[t] += c
    return v

def fijos_por_coche(cors, n=1024):
    """{coche: ([codigos fijos], candidatos)} y los que no van en los ST (los fijos de N). N: de sus corridas (todos los
    tramos). Los otros coches solo tienen corridas de algunos tramos: sus fijos son los de N menos los sprites que solo
    se ven con N en esos tramos (sus poses) mas los sprites que solo se ven con ese coche (las suyas), por cuadros; asi
    los ST no cambian"""
    porc = {c: [co for co in cors if co.coche == c] for c in sorted({co.coche for co in cors})}
    if 'N' not in porc: fc = {c: fijos(v, n) for c, v in porc.items()}; return fc, set.intersection(*[set(x[0]) for x in fc.values()])
    fc = {'N': fijos(porc['N'], n)}; fn = fc['N'][0]
    for c, cc in porc.items():
        if c == 'N': continue
        ks = sorted({k for co in cc for k in co.tramos})
        # solo sprites: las poses del coche (el plano que solo se ve con este coche es de su linea, no del coche)
        vc = vistos_carrera(cc, ks, (1,)); vn = vistos_carrera(porc['N'], ks); vns = vistos_carrera(porc['N'], ks, (1,))
        va = vistos_carrera(cc, ks)
        suyo = [t for t in vc if t not in vn]
        de_n = {t for t in vns if t not in va}
        base = [t for t in fn if t not in de_n]
        # todos por cuadros vistos con este coche (sus poses y lo comun: marcador, efectos); lo de N no visto en estos
        # tramos, al final en el orden de GAMEN
        pos = {t: i for i, t in enumerate(fn)}
        cand = sorted(set(suyo) | set(base), key=lambda t: (-va[t], pos.get(t, 1 << 20), t))
        fc[c] = (cand[:n], len(cand))
    return fc, set(fn)

def reparto(lineas, fj):
    """lineas [(corrida, tramo)] -> (por codigo, orden de VO + L2 sin los fijos, cierre): por primer avance FEFD08
    (minimo entre lineas) y primer cuadro relativo; al final, los codigos del plano nunca visibles (cierre barato)"""
    pc = por_codigo(junta([co.tramos[k]['res'] for co, k in lineas]))
    vistos = sorted((o[3], o[2], t) for t, o in pc.items() if not o[5] and t not in fj)
    cierre = sorted(t for t, o in pc.items() if o[5] and t not in fj)
    return pc, [t for _, _, t in vistos] + cierre, cierre

def lineas_tramo(cors, k):
    """lineas del tramo k para los ST: solo las corridas con el coche N si las hay (las poses de los otros coches van en
    su GAMEc y no deben crecer la L2)"""
    n = [(co, k) for co in cors if k in co.tramos and co.coche == 'N']
    return n or [(co, k) for co in cors if k in co.tramos]

def carrera(cors, out, margen, rot, nfij, romres, area):
    """GAMEc.PAK y ST00-11.PAK; cifras por tramo y G-RAM"""
    t0 = time.time(); psnr = []; cache = {}
    def reg(t, info, ivs, clave, av):
        if (clave, info[0]) not in cache: cache[(clave, info[0])] = paletas_estados(ivs, info[0]) or [paleta_viva(ivs, info[0])]
        r, p = tiles4.reg_b(t, info[0], cache[(clave, info[0])], info[1], min(av, 0xFFFF))
        if p is not None and p != float('inf'): psnr.append(p)
        return r
    fc, fj = fijos_por_coche(cors, nfij)
    for c in 'NLR':
        if c not in fc: print('GAME%s.PAK: sin corridas con ese coche (FE8124 = %04X): no se hace' % (c, [x for x, y in COCHES.items() if y == c][0])); continue
        cc = [co for co in cors if co.coche == c]; pc = por_codigo(junta([t['res'] for co in cc for t in co.tramos.values()]))
        ivs = [(co, [iv for t in co.tramos.values() for iv in t['iv']]) for co in cc]; pq = 0x0400 + 'NLR'.index(c)
        regs = [reg(t, pc[t], ivs, 'G' + c, 0xFFFF) for t in fc[c][0]]
        fn = os.path.join(out, 'GAME%s.PAK' % c); escribe_pak(fn, pq, [(2, sectores_b(regs, 4, pq, 0, None), len(regs))])
        print('GAME%s.PAK: %d B; %d fijos de %d candidatos, de %d corridas' % (c, os.path.getsize(fn), len(regs), fc[c][1], len(cc)))
    nfj = len(fj); nvo = HUECOS - nfj - rot; bancos = rombank.lee_romres(romres)[1]; filas = []
    for k in sorted({k for co in cors for k in co.tramos}):
        lin = lineas_tramo(cors, k); pq = 0x0300 + k
        claves = Counter(co.tramos[k]['clave'] for co, _ in lin); clave = claves.most_common(1)[0][0]
        if len(claves) > 1: print('AVISO: tramo %d con claves %s' % (k, dict(claves)))
        pc, orden, cierre = reparto(lin, fj); ivs = [(co, co.tramos[k]['iv']) for co, _ in lin]
        cm = [co for co, _ in lin if clave in co.mapas]
        mapa = por_codigo(junta([co.mapas[clave]['res'] for co in cm])); ivm = [(co, co.mapas[clave]['iv']) for co in cm]
        sec = [(1, sectores_b([reg(t, mapa[t], ivm, ('M', k), 0) for t in sorted(mapa)], 3, pq, 0, None), len(mapa))]
        for cl, tipo, ts in ((2, 4, orden[:nvo]), (3, 1, orden[nvo:])):
            sec.append((cl, sectores_b([reg(t, pc[t], ivs, ('T', k), pc[t][4] + margen) for t in ts], tipo, pq, 0, [pc[t][3] for t in ts]), len(ts)))
        trs = bancos.get(clave, []); sr = rombank.sectores_rom(trs, pq); sec.append((4, sr, len(trs)))
        fn = os.path.join(out, 'ST%02d.PAK' % k); escribe_pak(fn, pq, sec)
        vis = sum(1 for o in pc.values() if not o[5]); l2 = max(0, len(orden) - nvo); banco = sum(b - a + rombank.COLA for a, b in trs)
        filas.append((k, clave, len(lin), vis, nfj + len(orden), len(mapa), len(set(mapa) - fj), min(nvo, len(orden)), l2, len(cierre),
                      [len(x[1]) * 2048 for x in sec], os.path.getsize(fn), l2 * 144, banco))
        print('ST%02d.PAK hecho (%.1f s)' % (k, time.time() - t0), flush=True)
    print('\nfijos (no van en los ST): %d; VO = %d - fijos - ROT %d = %d huecos; area %d B (%.1f KB)' % (nfj, HUECOS, rot, nvo, area, area / 1024))
    print('tramo FEC3C6/C8 lin  vistos  N paq  mapa (sin fijos)    VO    L2 cierre | mapa B   VO B     L2 B  ROM B  fichero |  L2 RAM  banco  necesita  margen')
    for k, cl, nl, vis, n, nm, nm2, vo, l2, ci, bs, tam, l2b, banco in filas:
        nec = l2b + banco
        print('ST%02d    %d/%d    %3d %7d %6d %5d (%4d) %8d %5d %6d | %6d %6d %8d %6d %8d | %7d %6d %9d %+6.1f %%%s'
              % (k, cl[0], cl[1], nl, vis, n, nm, nm2, vo, l2, ci, bs[0], bs[1], bs[2], bs[3], tam, l2b, banco, nec, 100.0 * (area - nec) / nec,
                 '  MAPA > ROT' if nm2 > rot else ''))
    peor = max(filas, key=lambda f: f[12] + f[13]); nec = peor[12] + peor[13]
    print('G-RAM: area %d >= max(L2 + banco) x 1,02 = %d (ST%02d): %s (margen %+.1f %%)'
          % (area, int(nec * 1.02), peor[0], 'PASA' if area >= nec * 1.02 else 'FALLA', 100.0 * (area - nec) / nec))
    psnr.sort()
    if psnr: print('registros B de carrera con perdida: %d, PSNR mediana %.1f dB, minimo %.1f' % (len(psnr), psnr[len(psnr) // 2], psnr[0]))
    print('carrera: %.1f s' % (time.time() - t0))

def attract(blks, dumps, rss, out, margen, nvo):
    """MENUS.PAK y DEMO0-3.PAK"""
    t0 = time.time(); v = Vistos()
    for i, fn in enumerate(blks): v.suma(fn, i)
    pal = tiles4.Paletas(dumps)
    fases, _, _ = romstat.lee(rss); _, bancos, _ = romstat.conjuntos(fases)
    print('lectura: %.1f s; pantallas: %s' % (time.time() - t0, ' '.join('%s/%X' % k if k[0] == 'menu' else '%s%d' % k for k in sorted(v.inst))))
    psnr = []; cache = {}
    def reg_b(k, t, col, fl, av):
        """registro B del tile t con la paleta mas viva de las instancias de la pantalla k"""
        if (k, col) not in cache: cache[(k, col)] = pal.estados(col, v.inst[k])
        r, p = tiles4.reg_b(t, col, cache[(k, col)], fl, min(av, 0xFFFF))
        if p is not None and p != float('inf'): psnr.append(p)
        return r
    # MENUS.PAK: una seccion A por pantalla
    menus = sorted(k for k in v.inst if k[0] == 'menu'); secciones = []; cifras = []
    for i, k in enumerate(menus):
        pcod = por_codigo(v.resumen(k))
        cods = sorted(pcod, key=lambda t: (pcod[t][2], t))   # por primer cuadro en pantalla: la PS1 bloquea solo el principio
        if len(cods) > MAX_A: print('AVISO: la pantalla %X tiene %d tiles, mas de %d huecos A' % (k[1], len(cods), MAX_A))
        regs = [tilesbin.reg_a(t) for t in cods]
        ss = [tilesbin.sector(0, regs[j:j + 12], 0x0100 + i, j // 12) for j in range(0, len(regs), 12)]
        secciones.append((k[1], ss, len(regs))); cifras.append('%X: %d tiles, %d sectores' % (k[1], len(cods), len(ss)))
    escribe_pak(os.path.join(out, 'MENUS.PAK'), 0x0100, secciones)
    print('MENUS.PAK: %d pantallas, %d B\n  ' % (len(secciones), os.path.getsize(os.path.join(out, 'MENUS.PAK'))) + '\n  '.join(cifras))
    # DEMO0-3.PAK
    for r in range(4):
        kd = ('demo', r); km = ('mapa', r)
        if kd not in v.inst: print('DEMO%d.PAK: sin demo en las corridas' % r); continue
        pq = 0x0200 + r; secciones = []
        mapa = por_codigo(v.resumen(km)) if km in v.inst else {}
        regs = [reg_b(km, t, c, fl, 0) for t, (c, fl, _, _, _, _) in sorted(mapa.items())]
        secciones.append((1, sectores_b(regs, 3, pq, 0, None), len(regs)))
        demo = por_codigo(v.resumen(kd))
        vistos = sorted((o[2], t) for t, o in demo.items() if not o[5]); nunca = sorted(t for t, o in demo.items() if o[5])
        orden = [t for _, t in vistos] + nunca
        def regs_av(ts):
            return [reg_b(kd, t, demo[t][0], demo[t][1], demo[t][4] + margen) for t in ts], [demo[t][3] for t in ts]
        rv, av = regs_av(orden[:nvo]); secciones.append((2, sectores_b(rv, 4, pq, 0, av), len(rv)))
        rl, al = regs_av(orden[nvo:]); secciones.append((3, sectores_b(rl, 1, pq, 0, al), len(rl)))
        con = bancos.get((r, 0), (set(), set()))[1]
        sr = rombank.sectores_rom(rombank.tramos_bytes(con), pq); secciones.append((4, sr, len(romstat.tramos(con))))
        fn = os.path.join(out, 'DEMO%d.PAK' % r); escribe_pak(fn, pq, secciones)
        print('DEMO%d.PAK: %d B; mapa %d tiles (%d sectores), VO %d (%d), L2 %d (%d; %d nunca vistos), ROM %s en %d tramos (%d sectores); %d instancias de demo'
              % (r, os.path.getsize(fn), len(regs), len(secciones[0][1]), len(rv), len(secciones[1][1]), len(rl), len(secciones[2][1]), len(nunca),
                 romstat.kb(len(con)), len(romstat.tramos(con)), len(sr), len(v.inst[kd])))
    psnr.sort()
    if psnr: print('registros B con perdida: %d, PSNR mediana %.1f dB, minimo %.1f' % (len(psnr), psnr[len(psnr) // 2], psnr[0]))
    print('attract: %.1f s' % (time.time() - t0))

def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    def lista(op):
        if op not in a: return []
        i = a.index(op) + 1; r = []
        while i < len(a) and not a[i].startswith('-'): r.append(a[i]); i += 1
        return r
    def num(op, d): return int(a[a.index(op) + 1], 0) if op in a else d
    out = a[a.index('-o') + 1] if '-o' in a else os.path.join(WORK, 'cd')
    romres = a[a.index('--romres') + 1] if '--romres' in a else os.path.join(WORK, 'romres.txt')
    margen = num('--margen', 16); os.makedirs(out, exist_ok=True)
    if lista('--blk'): attract(lista('--blk'), lista('--dumps'), lista('--romstat'), out, margen, num('--vo', 4352))
    if lista('--tramos'):
        carrera(lee_corridas(lista('--tramos')), out, margen, num('--rot', 416), num('--fijos', 1024), romres, num('--area', 982 * 1024))

if __name__ == '__main__': main()
