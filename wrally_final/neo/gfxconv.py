"""Convierte los graficos de World Rally (16x16 4bpp) al formato C-ROM de Neo Geo."""
import numpy as np, os
R = '/home/claude/wrally/rom/'
def ld16(even, odd):
    e = np.frombuffer(open(R + even, 'rb').read(), np.uint8)
    o = np.frombuffer(open(R + odd, 'rb').read(), np.uint8)
    r = np.empty(len(e) * 2, np.uint8); r[0::2] = e; r[1::2] = o
    return r
def decode():
    reg = np.concatenate([ld16('worldr21.i13', 'worldr20.i11'), ld16('worldr19.i09', 'worldr18.i07')])
    bits = np.unpackbits(reg)              # MSB primero, como MAME
    half = len(bits) // 2
    n = half // 512
    t = np.arange(n)[:, None, None] * 512
    y = np.arange(16)[None, :, None] * 16
    x = np.concatenate([np.arange(8), 256 + np.arange(8)])[None, None, :]
    base = t + y + x
    pix = (bits[base] | (bits[base + 8] << 1) | (bits[half + base] << 2) | (bits[half + base + 8] << 3))
    return pix.astype(np.uint8)            # [n,16,16]
def to_neo(pix):
    """[n,16,16] -> region C intercalada (128 bytes/tile)"""
    n = pix.shape[0]
    out = np.zeros((n, 2, 16, 4), np.uint8)   # mitad (0=derecha,1=izquierda), fila, byte
    for half, x0 in ((0, 8), (1, 0)):
        p = pix[:, :, x0:x0 + 8].astype(np.uint16)     # [n,16,8]
        w = (1 << np.arange(8)).astype(np.uint16)
        for plane, bi in ((0, 0), (1, 2), (2, 1), (3, 3)):
            out[:, half, :, bi] = (((p >> plane) & 1) * w).sum(-1)
    return out.reshape(n * 128).tobytes()
if __name__ == '__main__':
    pix = decode()
    np.save('/home/claude/wrally/neo/build/tiles.npy', pix)
    # variante alta (prioridad): solo pens 8-15 de la capa 0 categoria 1
    hi = np.where(pix >= 8, pix, 0).astype(np.uint8)
    # variante efecto: pen 8+k -> pen k en damero (mezcla 50% con el color medio del banco k), pen 8 transparente
    # tramado ordenado (Bayer 4x4): el banco k ocupa DITH_N[k]/16 de los pixeles. k = 1 sigue en
    # damero (50%: sombras de dia y borde exterior de los faros); los demas, de menos a mas
    # densidad hacia el centro del haz. video.c (effect_bank) compensa el color con la densidad
    # (la media de cada zona sigue siendo fondo + luz del banco). Debe coincidir con dith_n[]
    yy, xx = np.mgrid[0:16, 0:16]
    bay = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])[yy & 3, xx & 3]
    DITH_N = [0, 8, 4, 6, 8, 10, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0]
    k = np.where(pix >= 9, pix - 8, 0)
    lit = bay[None] < np.array(DITH_N)[k]
    # fase 4m: los tiles de haz de faros (cualquier tile con pens 10-15: las sombras solo usan el 9;
    # hay juegos de haz distintos para cada direccion del coche, no solo 0x3cf3-0x3d3d) llevan un
    # tramado muy abierto: 1-3 pixeles de cada 16. Las sombras siguen al 50%.
    DITH_B = [0, 1, 1, 1, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0]
    beam = (pix >= 10).any((1, 2))
    lit = np.where(beam[:, None, None], bay[None] < np.array(DITH_B)[k], lit)
    sh = np.where((pix >= 9) & lit, pix - 8, 0).astype(np.uint8)
    c = to_neo(np.concatenate([pix, hi, sh]))
    c += bytes(0x800000 - len(c))
    open('/home/claude/wrally/neo/build/crom.bin', 'wb').write(c)
    print(pix.shape, len(c))
    # vista previa de los primeros 1024 tiles en gris
    from PIL import Image
    im = Image.new('L', (32 * 16, 32 * 16))
    a = np.zeros((512, 512), np.uint8)
    for i in range(1024):
        ty, tx = divmod(i, 32)
        a[ty*16:ty*16+16, tx*16:tx*16+16] = pix[0x1000 + i] * 16
    Image.fromarray(a).save('/tmp/claude-0/tiles.png')
