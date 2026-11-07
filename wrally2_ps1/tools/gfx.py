"""World Rally 2: region "gfx1" de MAME (0xA00000 bytes, graficos + muestras GAE1) a partir de las ROM.
Juego de mascaras (ic68/ic69/ic70, init_wrally2 de gaelco2_m.cpp) o de EPROM (14 x 27C040, ROM_START(wrally2a)).
Formato: 5 planos de 0x200000 bytes (RGN_FRAC(1,5)), 65536 tiles de 16x16.
  plano bit0 0x000000 (ic69 pares)  tiles 0x8000-0xFFFF = banco de sonido 0 (0x100000-0x1FFFFF)
  plano bit1 0x200000 (ic69 impares) tiles 0x8000-0xFFFF = banco de sonido 1 (0x300000-0x3FFFFF)
  plano bit2 0x400000 (ic70 pares, 0x500000+ = 0)  bit3 0x600000 (ic70 impares, 0x700000+ = 0)
  plano bit4 0x800000 (ic68, 0x900000+ = 0)
Tile t, plano p: 32 bytes en p*0x200000 + t*32; byte 0..15 = fila y pixeles x0..7, 16..31 = x8..15, MSB a la izquierda.
  gfx.py tile  <gfx1.bin> <codigo> [...]           tiles en hex (tinta 0..31)
  gfx.py sheet <gfx1.bin> <primero> <n> <out.png>  hoja en grises de n tiles (32 por fila)"""
import sys

PLANO = 0x200000
TAM = 5 * PLANO
EPROM = [('wr2.16d', 0x000000), ('wr2.17d', 0x080000), ('wr2.18d', 0x100000), ('wr2.19d', 0x180000),
         ('wr2.09d', 0x200000), ('wr2.10d', 0x280000), ('wr2.11d', 0x300000), ('wr2.12d', 0x380000),
         ('wr2.01d', 0x400000), ('wr2.02d', 0x480000), ('wr2.20d', 0x600000), ('wr2.13d', 0x680000),
         ('wr2.21d', 0x800000), ('wr2.14d', 0x880000)]

def construir(rs):
    """gfx1 desde un RomSet: mascaras si estan, si no las EPROM de wrally2a"""
    g = bytearray(TAM)
    if rs.has('wr2_ic69.ic69'):
        ic68, ic69, ic70 = rs.read('wr2_ic68.ic68'), rs.read('wr2_ic69.ic69'), rs.read('wr2_ic70.ic70')
        assert len(ic68) == 0x100000 and len(ic69) == 0x400000 and len(ic70) == 0x200000
        g[0x000000:0x200000] = ic69[0::2]; g[0x200000:0x400000] = ic69[1::2]
        g[0x400000:0x500000] = ic70[0::2]; g[0x600000:0x700000] = ic70[1::2]
        g[0x800000:0x900000] = ic68
        return g, 'mascaras'
    for n, o in EPROM:
        b = rs.read(n); assert len(b) == 0x80000, n
        g[o:o + 0x80000] = b
    return g, 'eprom'

def tile(g, t):
    """16x16 tintas (0..31) del tile t (modulo 65536 como el gfx_element de MAME)"""
    t &= 0xFFFF
    px = [[0] * 16 for _ in range(16)]
    for p in range(5):
        base = p * PLANO + t * 32
        for y in range(16):
            for h in range(2):
                b = g[base + h * 16 + y]
                for i in range(8):
                    if b & (0x80 >> i): px[y][h * 8 + i] |= 1 << p
    return px

def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    g = open(a[1], 'rb').read()
    if a[0] == 'tile':
        for c in a[2:]:
            print('tile %05X' % int(c, 0))
            for row in tile(g, int(c, 0)): print(' ' + ' '.join('%02X' % v for v in row))
    elif a[0] == 'sheet':
        from PIL import Image
        first = int(a[2], 0); n = int(a[3], 0); rows = (n + 31) // 32
        im = Image.new('L', (32 * 17, rows * 17), 40)
        for k in range(n):
            px = tile(g, first + k); ox, oy = (k % 32) * 17, (k // 32) * 17
            for y in range(16):
                for x in range(16): im.putpixel((ox + x, oy + y), px[y][x] * 8)
        im.save(a[4])

if __name__ == '__main__': main()
