"""romstat.py fichero.txt [...]: junta los ROMSTAT del banco (rc/rc_host_rom) y da los conjuntos de la ROM del 68000 en
bloques de 256 B (diseno 3.3):
- antes del fin de la suma del arranque (PC 0x2616) todo viene de ROM.BIN entera: no cuenta;
- residente = lo leido despues en los modos que no son carrera (1) ni demo (5) (menus, mapa, resultado, negro...)
  y lo leido de la zona de codigo 0-1B7FF en cualquier modo;
- banco de cada tramo (clave FEC3C6/FEC3C8) = lo leido en carrera y demo con esa clave fuera del residente, con un
  bloque mas a cada lado de cada tramo contiguo.
Con -v, los bloques de cada modo. Con -o romres.txt escribe el residente ('R inicio fin' por tramo, en hexadecimal) y los
bancos ('B fec3c6 fec3c8 inicio fin'): lo leen rc68.py (lecturas a direccion fija) y rc_host ROMCHK."""
import sys
from collections import defaultdict

CARRERA = (1, 5)
ZONA = 0x1B8                      # bloques de la zona de codigo 0-1B7FF (vectores, tablas del sonido...)

def lee(fs):
    fases = defaultdict(set)          # (modo, t6, t8) -> bloques, solo tras la suma
    antes = set(); cuadros = 0
    for fn in fs:
        for l in open(fn):
            p = l.split()
            if not p or p[0] == 'F': continue
            fr, modo, t6, t8, suma, b = int(p[0]), int(p[1], 16), int(p[2], 16), int(p[3], 16), int(p[4]), int(p[5], 16)
            cuadros = max(cuadros, fr)
            (fases[(modo, t6, t8)] if suma else antes).add(b)
    return fases, antes, cuadros

def tramos(bl):
    """bloques -> [(inicio, fin)] contiguos"""
    r = []
    for b in sorted(bl):
        if r and b == r[-1][1] + 1: r[-1][1] = b
        else: r.append([b, b])
    return r

def kb(n): return '%.1f KB' % (n * 256 / 1024)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    out = sys.argv[sys.argv.index('-o') + 1] if '-o' in sys.argv else None
    if out: args.remove(out)
    fases, antes, cuadros = lee(args)
    pormodo = defaultdict(set)
    for (m, a, b), s in fases.items(): pormodo[m] |= s
    print('antes del fin de la suma: %d bloques (%s)' % (len(antes), kb(len(antes))))
    for m in sorted(pormodo):
        print('modo %X: %d bloques (%s)%s' % (m, len(pormodo[m]), kb(len(pormodo[m])), ' carrera/demo' if m in CARRERA else ''))
        if '-v' in sys.argv: print('   ', ' '.join('%05X-%05X' % (a << 8, (b << 8) + 255) for a, b in tramos(pormodo[m])))
    res = set().union(*[s for m, s in pormodo.items() if m not in CARRERA]) if pormodo else set()
    res |= {b for s in pormodo.values() for b in s if b < ZONA}   # la zona de codigo leida como datos, en todo modo
    zona = {b for b in res if b < ZONA}
    print('residente: %d bloques (%s), de ellos %s en la zona de codigo 0-1B7FF; %d tramos contiguos'
          % (len(res), kb(len(res)), kb(len(zona)), len(tramos(res))))
    claves = sorted({(a, b) for (m, a, b) in fases if m in CARRERA})
    total = set(); bancos = []
    for a, b in claves:
        s = set().union(*[fases[(m, a, b)] for m in CARRERA if (m, a, b) in fases]) - res
        con = set()
        for x, y in tramos(s): con |= set(range(max(0, x - 1), min(0xDD7, y + 1) + 1))
        con -= res; total |= con; bancos.append(((a, b), con))
        ms = '/'.join('%X' % m for m in CARRERA if (m, a, b) in fases)
        print('banco FEC3C6=%04X FEC3C8=%04X (modos %s): %d bloques leidos fuera del residente, %s con los bordes'
              % (a, b, ms, len(s), kb(len(con))))
    print('union de los bancos: %s; fuera de todo (no se lee tras la suma): %s de 0xDD8 bloques'
          % (kb(len(total)), kb(0xDD8 - len(total | res))))
    print('cuadros: %d' % cuadros)
    if out:
        with open(out, 'w') as f:
            for x, y in tramos(res): f.write('R %05x %05x\n' % (x << 8, (y << 8) + 256))
            for (a, b), con in bancos: 
                for x, y in tramos(con): f.write('B %x %x %05x %05x\n' % (a, b, x << 8, (y << 8) + 256))
        print('escrito', out)

main()
