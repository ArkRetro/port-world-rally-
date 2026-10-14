"""tilesbin.py [--a salida] [--b salida receta.bin]: ficheros de tiles por codigo (doc/formatos.md 2.3) desde
work/gfx1.bin: TILESA.BIN (registros A exactos, 12 por sector) y, si hay recetas de tiles4.py, TILES.BIN (registros B,
14 por sector). Tambien exporta reg_a(t) y sector() para los demas constructores de paquetes."""
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
        for i in range(0, 32768, 12):
            f.write(sector(0, [reg_a(t) for t in range(i, i + 12)], 0x0500, i // 12))
    print(out, os.path.getsize(out) >> 10, 'KB')

if __name__ == '__main__':
    a = sys.argv[1:]
    if '--a' in a: tilesa(a[a.index('--a') + 1])
    if not a: print(__doc__)
