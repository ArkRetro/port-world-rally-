"""verpak.py fichero.PAK|.BIN [...] [-v] [-r]: lista y comprueba los paquetes del CD (formatos.md 2, 3 y 5): indice
(claves, sectores, registros), secciones y sectores (cabecera WR2T, tipo, paquete, indice seguido, cuentas, 2 048 B);
registros A descodificados y comparados con el tile de gfx1.bin; registros B: receta, banderas y, si son exactos
(bit 2, bit 3 o <= 15 pens), comparados con el tile; tramos de ROM comparados con prog.bin. Con -v, los sectores uno a
uno; con -r, cada registro. Carrera: en un paquete ningun codigo repetido entre VO y L2; GAMEc.PAK (0x04xx) con
avance 0xFFFF y <= 1 024 registros; si se dan GAMEc y STkk juntos, ningun codigo de los GAME en el VO/L2 de los ST y
fijos + VO <= 4 352 huecos. Sale con 1 si algo falla."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from collections import Counter
import tilesbin, tiles4, rombank

TIPO = {0: 'A', 1: 'L2 (B)', 2: 'ROM', 3: 'mapa (B)', 4: 'VO (B)', 5: 'indice'}
POR = {0: 12, 1: 14, 3: 14, 4: 14}
fallos = 0
def falla(m):
    global fallos; fallos += 1; print('FALLO:', m)

def comprueba_a(r):
    t = struct.unpack_from('<H', r, 0)[0]; px = tiles4.tile(t)
    for y in range(16):
        for x in range(16):
            p = (r[2 + y * 8 + x // 2] >> (4 if x & 1 else 0)) & 15 | (16 if r[130 + y * 2 + x // 8] & (0x80 >> (x & 7)) else 0)
            if p != px[y][x]: falla('registro A %04X: pixel (%d, %d) = %d, tile %d' % (t, x, y, p, px[y][x])); return t
    return t

def comprueba_b(r, verboso):
    t, idx, rec, c, fl, av = tiles4.desempaqueta(r); px = tiles4.tile(t); idx = np.array(idx)
    pens = np.array([0] + rec)[idx]                     # pen que dibuja cada pixel con la receta
    if verboso: print('   B %04X pal %d banderas %02X avance %04X receta %s' % (t, c, fl, av, ' '.join('%d' % p for p in rec)))
    if c > 63: falla('registro B %04X: paleta %d' % (t, c))
    if (idx != 0).sum() != (px != 0).sum(): falla('registro B %04X: la transparencia no coincide' % t)
    exacto = fl & (tiles4.IDXPEN | tiles4.ALTA) or len(set(px[px != 0].tolist())) <= 15
    if fl & tiles4.IDXPEN and not np.array_equal(idx, px): falla('registro B %04X: bit 2 pero indice != pen' % t)
    if fl & tiles4.ALTA and (not np.array_equal(idx + 16, px) or (px == 0).any()): falla('registro B %04X: bit 3 pero indice + 16 != pen o hay pen 0' % t)
    if exacto and not np.array_equal(pens, px): falla('registro B %04X: receta no exacta (pens %s)' % (t, sorted(set(px.ravel().tolist()))))
    if fl & tiles4.EFECTO and not fl & tiles4.IDXPEN: falla('registro B %04X: efecto sin indice = pen' % t)
    return t

def main():
    a = sys.argv[1:]; verboso = '-v' in a; regs = '-r' in a; games = []; sts = []
    for fn in [x for x in a if not x.startswith('-')]:
        d = open(fn, 'rb').read(); n = len(d) // 2048; cods = []; nvo = 0; vos = []
        print('%s: %d B, %d sectores' % (fn, len(d), n))
        if len(d) % 2048: falla('tamano no multiplo de 2048')
        secs = {}; ult = {}; nsec = 0
        for i in range(n):
            s = d[i * 2048:(i + 1) * 2048]
            if s[:4] != b'WR2T': falla('sector %d sin WR2T' % i); continue
            tipo, nr = s[4], s[5]; pq, idx, av = struct.unpack_from('<HII', s, 6)
            k = (tipo, pq)
            if k in ult and ult[k] != idx - 1 and tipo != 5: falla('sector %d: indice %d, el anterior del tipo %d fue %d' % (i, idx, tipo, ult[k]))
            ult[k] = idx; secs.setdefault(k, []).append(i)
            if tipo == 5:
                ent = [struct.unpack_from('<IIII', s, 32 + 16 * j) for j in range(125)]
                print('  indice (sector %d, paquete %04X): %d entradas' % (i, pq, nr))
                for cl, s0, ns, nreg in ent:
                    if cl == 0xFFFFFFFF: break
                    print('    clave %08X: sectores %d-%d (%d), registros %d' % (cl, s0, s0 + ns - 1, ns, nreg))
                    if s0 + ns > n: falla('la entrada %08X sale del fichero' % cl)
                    else:
                        tipos = {d[j * 2048 + 4] for j in range(s0, s0 + ns)}
                        if tipos == {2}:            # ROM: los registros son tramos (sectores seguidos con direccion contigua)
                            fin = -1; suma = 0
                            for j in range(s0, s0 + ns):
                                o = j * 2048 + 32; dirr, nb = struct.unpack_from('<II', d, j * 2048 + 16)
                                for k in range(d[j * 2048 + 5] + 1):   # trozos del sector (rombank.sectores_rom)
                                    if k: dirr, nb = struct.unpack_from('<II', d, o); o += 8
                                    suma += dirr != fin; fin = dirr + nb; o += nb
                        else: suma = sum(d[j * 2048 + 5] for j in range(s0, s0 + ns))
                        if len(tipos) > 1: falla('la seccion %08X mezcla tipos %s' % (cl, tipos))
                        if suma != nreg: falla('la seccion %08X dice %d registros y los sectores suman %d' % (cl, nreg, suma))
                        if nreg > 2560 and tipos == {0}: print('  AVISO: %08X tiene %d registros A, mas de 2 560 huecos' % (cl, nreg))
                continue
            if tipo in POR:
                if nr > POR[tipo]: falla('sector %d: %d registros de tipo %d' % (i, nr, tipo))
                tam = 162 if tipo == 0 else 144
                if any(s[32 + nr * tam:]): falla('sector %d: resto no nulo' % i)
                if verboso: print('  sector %d: %s, paquete %04X, idx %d, %d registros, primer avance %04X' % (i, TIPO[tipo], pq, idx, nr, av))
                for j in range(nr):
                    r = s[32 + j * tam:32 + (j + 1) * tam]
                    if tipo == 0:
                        t = comprueba_a(r)
                        if regs: print('   A %04X' % t)
                        if pq == 0x0500 and t != idx * 12 + j: falla('TILESA: sector %d registro %d es %04X' % (idx, j, t))
                    else:
                        t = comprueba_b(r, regs)
                        if tipo in (1, 4) and pq != 0x0501:
                            cods.append(t); nvo += tipo == 4
                            if tipo == 4: vos.append(t)
                        if pq >> 8 == 4 and struct.unpack_from('<H', r, 142)[0] != 0xFFFF: falla('GAME: registro %04X sin avance 0xFFFF' % t)
                        if pq == 0x0501 and t != idx * 14 + j: falla('TILES: sector %d registro %d es %04X' % (idx, j, t))
                        if tipo == 3 and av: falla('sector %d (mapa) con avance %d' % (i, av))
            elif tipo == 2:                   # trozos: el primero en la cabecera, los nr de detras con 8 bytes delante
                dirr, nb = struct.unpack_from('<II', s, 16); o = 32
                for k in range(nr + 1):
                    if k: dirr, nb = struct.unpack_from('<II', s, o); o += 8
                    if nb & 3 or o + nb > 2048: falla('sector %d (ROM): trozo %d de %d bytes' % (i, k, nb)); break
                    if s[o:o + nb] != rombank.prog()[dirr:dirr + nb]: falla('sector %d (ROM): los bytes de %06X no son los de prog.bin' % (i, dirr))
                    if verboso: print('  sector %d: ROM %06X-%06X (%d B), paquete %04X, idx %d' % (i, dirr, dirr + nb - 1, nb, pq, idx))
                    o += nb
                if any(s[o:]): falla('sector %d (ROM): resto no nulo' % i)
            else: falla('sector %d: tipo %d' % (i, tipo))
        for (tipo, pq), l in sorted(secs.items()):
            print('  tipo %d (%s) paquete %04X: %d sectores' % (tipo, TIPO.get(tipo, '?'), pq, len(l)))
        rep = [c for c, k in Counter(cods).items() if k > 1]
        if rep: falla('%s: %d codigos repetidos entre VO y L2 (%s)' % (fn, len(rep), ' '.join('%04X' % c for c in rep[:8])))
        pqs = {pq >> 8 for _, pq in secs}
        if 4 in pqs:
            games.append(set(vos))           # los fijos (VO); la L2 del coche (L, R) va al area
            if len(vos) > 1024: falla('%s: %d fijos, mas de 1 024 huecos' % (fn, len(vos)))
        if 3 in pqs: sts.append((fn, set(cods), nvo))
    if games and sts:                   # los fijos de todos los coches no van en los tramos
        fj = set.intersection(*games)
        for fn, c, nvo in sts:
            if c & fj: falla('%s: %d codigos fijos en VO/L2' % (fn, len(c & fj)))
            if nvo + max(len(g) for g in games) > 4352: falla('%s: fijos + VO = %d > 4 352' % (fn, nvo + max(len(g) for g in games)))
        print('carrera: %d GAME, %d ST; fijos comunes %d; VO de %d a %d' % (len(games), len(sts), len(fj), min(x[2] for x in sts), max(x[2] for x in sts)))
    print('fallos: %d' % fallos)
    sys.exit(1 if fallos else 0)

if __name__ == '__main__': main()
