"""tilesbin.py [--a TILESA.BIN] [--b TILES.BIN --blk blk.bin ... --dumps dumps.bin ...]: ficheros de tiles por codigo
(doc/formatos.md 2.3) desde work/gfx1.bin: TILESA.BIN (registros A exactos, 12 por sector) y TILES.BIN (registros B de
tiles4.py, 14 por sector: con la receta de la paleta mas vista en los BLKLOG si el tile se vio; si no, exacta si cabe
en 15 pens o fundida con la paleta 0 del ultimo volcado; avance 0xFFFF). Tambien exporta reg_a(t) y sector() para los
demas constructores de paquetes."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfx

GFX = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work', 'gfx1.bin')
SEC = 2048
_g = None
def g():
    global _g
    if _g is None: _g = open(GFX, 'rb').read()
    return _g

def reg_a(t):
    """registro A de 162 bytes del tile t"""
    px = gfx.tile(g(), t); r = bytearray(162); struct.pack_into('<H', r, 0, t & 0xFFFF)
    for y in range(16):
        for x in range(16):
            p = px[y][x]
            r[2 + y * 8 + x // 2] |= (p & 15) << (4 if x & 1 else 0)
            if p & 16: r[130 + y * 2 + x // 8] |= 0x80 >> (x & 7)
    return bytes(r)

def sector(tipo, regs, paquete, idx, avance=0, extra=b''):
    """un sector de paquete: cabecera de 32 bytes y los registros seguidos"""
    h = b'WR2T' + bytes([tipo, len(regs)]) + struct.pack('<HII', paquete, idx, avance) + extra
    s = h + bytes(32 - len(h)) + b''.join(regs)
    assert len(s) <= SEC, len(s)
    return s + bytes(SEC - len(s))

def tilesa(out):
    with open(out, 'wb') as f:
        for i in range(0, 32768, 12):           # el ultimo sector lleva 8 registros
            f.write(sector(0, [reg_a(t) for t in range(i, min(i + 12, 32768))], 0x0500, i // 12))
    print(out, os.path.getsize(out) >> 10, 'KB')

def tilesb(out, blks, dumps):
    import tiles4
    st = tiles4.cuenta(blks); pal = tiles4.Paletas(dumps); cache = {}; vistos = perdida = 0
    with open(out, 'wb') as f:
        for i in range(0, 32768, 14):
            regs = []
            for t in range(i, i + 14):
                if t in st:
                    cn, fl, _, _, iv = st[t]; c = cn.most_common(1)[0][0]; fr = pal.elige(c, iv); vistos += 1
                else: c, fl, fr = 0, 0, int(pal.cuadros[-1])
                if (fr, c) not in cache: cache[(fr, c)] = pal.paleta(fr, c)
                r, p = tiles4.reg_b(t, c, cache[(fr, c)], fl, 0xFFFF); regs.append(r); perdida += p is not None and p != float('inf')
            f.write(sector(1, regs, 0x0501, i // 14))
    print(out, os.path.getsize(out) >> 10, 'KB; %d tiles vistos en los BLKLOG, %d con perdida' % (vistos, perdida))

if __name__ == '__main__':
    a = sys.argv[1:]
    def lista(op):
        r = []; i = a.index(op) + 1 if op in a else len(a)
        while i < len(a) and not a[i].startswith('-'): r.append(a[i]); i += 1
        return r
    if '--a' in a: tilesa(a[a.index('--a') + 1])
    if '--b' in a: tilesb(a[a.index('--b') + 1], lista('--blk'), lista('--dumps'))
    if not a: print(__doc__)
