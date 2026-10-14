"""rombank.py romstat.txt [...] [-o work/cd] [--romres work/romres.txt]: la ROM del 68000 para el CD (formatos.md 3 y
5, diseno 3): ROM.BIN (prog.bin 0x000000-0x0DD7FF tal cual, para el arranque y las trampas), ROMRES.BIN (los tramos
residentes de tools/romstat.py en sectores de tipo 2, paquete 0x0600) y work/romres.txt (lineas 'R inicio fin' y
'B fec3c6 fec3c8 inicio fin', para rc68.py). Cada tramo lleva 4 bytes de cola (los del vecino, diseno 3.1) y va en
sectores seguidos; la cabecera de cada sector dice la direccion de su primer byte y cuantos bytes lleva.
Exporta sectores_rom() para la seccion ROM de los paquetes (paquetes.py)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import romstat, tilesbin

WORK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work')
FIN = 0xDD800                     # nadie lee 0x0DD800-0x0FFFFF (diseno 3.3)
COLA = 4
POR_SECTOR = 2016

_prog = None
def prog():
    global _prog
    if _prog is None: _prog = open(os.path.join(WORK, 'prog.bin'), 'rb').read()
    return _prog

def sectores_rom(trs, paquete, idx0=0):
    """sectores de tipo 2 con los tramos [(inicio, fin)] (fin excluido, bloques de 256 B) y su cola, en orden"""
    out = []; p = prog()
    for a, b in trs:
        d = p[a:b + COLA]; o = 0
        while o < len(d):
            n = min(POR_SECTOR, len(d) - o)
            s = bytearray(tilesbin.sector(2, [d[o:o + n]], paquete, idx0 + len(out), 0, (a + o).to_bytes(4, 'little') + n.to_bytes(4, 'little')))
            s[5] = 0; out.append(bytes(s))
            o += n
    return out

def tramos_bytes(bl): return [(x << 8, (y << 8) + 256) for x, y in romstat.tramos(bl)]

def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    out = a[a.index('-o') + 1] if '-o' in a else os.path.join(WORK, 'cd')
    rr = a[a.index('--romres') + 1] if '--romres' in a else os.path.join(WORK, 'romres.txt')
    fs = [x for i, x in enumerate(a) if not x.startswith('-') and a[i - 1] not in ('-o', '--romres')]
    fases, antes, cuadros = romstat.lee(fs); res, bancos, _ = romstat.conjuntos(fases)
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'ROM.BIN'), 'wb').write(prog()[:FIN])
    secs = sectores_rom(tramos_bytes(res), 0x0600)
    open(os.path.join(out, 'ROMRES.BIN'), 'wb').write(b''.join(secs))
    romstat.escribe(rr, res, bancos)
    print('ROM.BIN %d B; ROMRES.BIN %d tramos, %d bloques (%s), %d sectores (%d B); %s' % (FIN, len(romstat.tramos(res)), len(res), romstat.kb(len(res)), len(secs), len(secs) * 2048, rr))
    for (c6, c8), (s, con) in sorted(bancos.items()):
        print('  banco FEC3C6=%X FEC3C8=%X: %d tramos, %s' % (c6, c8, len(romstat.tramos(con)), romstat.kb(len(con))))

if __name__ == '__main__': main()
