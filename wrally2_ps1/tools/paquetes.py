"""paquetes.py --blk blk.bin [...] --dumps dumps.bin [...] --romstat romstat.txt [...] [-o work/cd] [--margen 16]
[--vo 4352]: los paquetes de video del attract (formatos.md 3 y 5, diseno 10) desde las corridas del banco:
- MENUS.PAK: una seccion A (tipo 0) por pantalla (clave modo << 16 | FEC146; todo lo visible: plano y sprites) de los
  modos que no son carrera (1), demo (5), mapa de demo (9) ni negro (0). Avisa si una pantalla pasa de 2 560 tiles.
- DEMOr.PAK (r = FEC3C6 de la demo): secciones mapa (1, tipo 3: lo visto en modo 9 con FEC146 0x19 + r), VO (2, tipo 4:
  los primeros tiles B de la demo por primer cuadro de uso hasta llenar los huecos), L2 (3, tipo 1: el resto y, al
  final, los codigos escritos en el plano que nunca se vieron) y ROM (4, tipo 2: el banco del tramo (r, 0) de ROMSTAT).
  Registros B con la paleta mas vista en esa pantalla y, en los bytes 142-143, el ultimo avance FEFD08 + margen.
Las corridas son deterministas desde el encendido: se dan los BLKLOG, volcados y ROMSTAT de la corrida larga del
attract y de la partida (lo que sigue a la partida son ciclos con otra semilla)."""
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
    def suma(self, fn):
        cab, ent = lee_blk(fn); prev = None
        for (fr, modo, f146, c6, fd08, n), e in zip(cab, ent):
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
        e = np.concatenate([x[0] for x in d]); fr = np.concatenate([np.full(len(x[0]), x[1]) for x in d])
        rel = np.concatenate([np.full(len(x[0]), x[2]) for x in d]); av = np.concatenate([np.full(len(x[0]), x[3]) for x in d])
        key = (e & 0xFFFF).astype(np.int64) << 16 | ((e >> 28) & 3) << 8 | (e >> 16) & 0x7F
        u, inv, cn = np.unique(key, return_inverse=True, return_counts=True)
        r0 = np.full(len(u), 1 << 30); a0 = np.full(len(u), 1 << 30); a1 = np.full(len(u), -1)
        np.minimum.at(r0, inv, rel); np.minimum.at(a0, inv, av); np.maximum.at(a1, inv, av)
        return {(k >> 16, k & 0x7F, (k >> 8) & 3): [c, r, p, q] for k, c, r, p, q in zip(u.tolist(), cn.tolist(), r0.tolist(), a0.tolist(), a1.tolist())}

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

def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    def lista(op):
        if op not in a: return []
        i = a.index(op) + 1; r = []
        while i < len(a) and not a[i].startswith('-'): r.append(a[i]); i += 1
        return r
    out = a[a.index('-o') + 1] if '-o' in a else os.path.join(WORK, 'cd')
    margen = int(a[a.index('--margen') + 1], 0) if '--margen' in a else 16
    nvo = int(a[a.index('--vo') + 1], 0) if '--vo' in a else 4352
    os.makedirs(out, exist_ok=True); t0 = time.time()
    v = Vistos()
    for fn in lista('--blk'): v.suma(fn)
    pal = tiles4.Paletas(lista('--dumps'))
    fases, _, _ = romstat.lee(lista('--romstat')); _, bancos, _ = romstat.conjuntos(fases)
    print('lectura: %.1f s; pantallas: %s' % (time.time() - t0, ' '.join('%s/%X' % k if k[0] == 'menu' else '%s%d' % k for k in sorted(v.inst))))
    psnr = []; cache = {}
    def reg_b(k, t, col, fl, av):
        """registro B del tile t con la paleta mas viva de las instancias de la pantalla k"""
        if (k, col) not in cache: cache[(k, col)] = pal.paleta(pal.elige(col, v.inst[k]), col)
        r, p = tiles4.reg_b(t, col, cache[(k, col)], fl, min(av, 0xFFFF))
        if p is not None and p != float('inf'): psnr.append(p)
        return r
    # MENUS.PAK: una seccion A por pantalla
    menus = sorted(k for k in v.inst if k[0] == 'menu'); secciones = []; cifras = []
    for i, k in enumerate(menus):
        cods = sorted(por_codigo(v.resumen(k)))
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
    print('tiempo %.1f s' % (time.time() - t0))

if __name__ == '__main__': main()
