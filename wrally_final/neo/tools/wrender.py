"""Renderizador de referencia de World Rally (algoritmo de MAME) desde volcados de memoria."""
import numpy as np, struct, sys
from PIL import Image
T = np.load('/home/claude/wrally/neo/build/tiles.npy')
def load(prefix):
    rd = lambda n: np.frombuffer(open(prefix + n, 'rb').read(), '>u2').astype(np.int32)
    return rd('d_vram.bin'), rd('d_pal.bin'), rd('d_spr.bin'), rd('d_vregs.bin')
def pal_rgb(pal):
    b = (pal >> 8) & 15; r = (pal >> 4) & 15; g = pal & 15
    return (np.stack([r, g, b], -1) * 17).astype(np.uint8)
def tilemap(vram, layer):
    """devuelve (pix[512,1024] pen, color[512,1024], cat[512,1024])"""
    pen = np.zeros((512, 1024), np.int32); col = np.zeros_like(pen); cat = np.zeros_like(pen)
    for i in range(2048):
        d = vram[(layer << 12) + 2 * i]; d2 = vram[(layer << 12) + 2 * i + 1]
        t = T[d & 0x3fff]
        if d2 & 0x40: t = t[:, ::-1]
        if d2 & 0x80: t = t[::-1, :]
        r, c = divmod(i, 64)
        pen[r*16:r*16+16, c*16:c*16+16] = t
        col[r*16:r*16+16, c*16:c*16+16] = d2 & 0x1f
        cat[r*16:r*16+16, c*16:c*16+16] = (d2 >> 5) & 1
    return pen, col, cat
def scrolled(a, sx, sy, W=384, H=256):
    ys = (np.arange(H) + sy) & 511; xs = (np.arange(W) + sx) & 1023
    return a[ys][:, xs]
def render(prefix, which=None):
    vram, pal, spr, vregs = load(prefix)
    P = pal_rgb(pal)
    L0 = [scrolled(x, vregs[1] + 4, vregs[0]) for x in tilemap(vram, 0)]
    L1 = [scrolled(x, vregs[3], vregs[2]) for x in tilemap(vram, 1)]
    H, W = 256, 384
    bmp = np.zeros((H, W), np.int32)
    def draw(L, catv, penmask):
        pen, col, cat = L
        m = penmask(pen)
        if catv is not None: m &= (cat == catv)
        bmp[m] = (col[m] << 4) | pen[m]
    if which in (None, 'L1'): draw(L1, None, lambda p: np.ones_like(p, bool))
    if which in (None, 'L0'):
        draw(L0, 0, lambda p: (p >= 1) & (p <= 7)); draw(L0, 0, lambda p: p >= 8)
    if which is None:
        draw(L1, 1, lambda p: p > 0)
        draw(L0, 1, lambda p: (p >= 1) & (p <= 7))
    # sprites
    sb = np.zeros((512, 1024), np.int32)
    if which in (None, 'SPR', 'SPRN', 'SPRE'):
        for i in range(3, (0x1000 - 6) // 2, 4):
            w0, w2, w3 = spr[i], spr[i + 2], spr[i + 3]
            sx = w2 & 0x3ff; sy = (240 - (w0 & 0xff)) & 0xff
            num = w3 & 0x3fff; color = (w2 & 0x7c00) >> 10; eff = (color >> 4) & 1; color &= 15
            attr = (w0 & 0xfe00) >> 9
            if which == 'SPRN' and eff: continue
            if which == 'SPRE' and not eff: continue
            hp = 1 if num >= 0x3700 else 0
            g = T[num]
            if attr & 0x20: g = g[:, ::-1]
            if attr & 0x40: g = g[::-1, :]
            for py in range(16):
                y = (sy + py) & 0x1ff
                for px in range(16):
                    x = (((sx + px) & 0x3ff) - 0x0f) & 0x3ff
                    p = int(g[py, px])
                    if not eff:
                        if p: sb[y, x] = p | (color << 4) | (hp << 8)
                    else:
                        if p < 8: continue
                        s = sb[y, x]
                        if s: sb[y, x] = s | ((p - 8) << 12) | 0x200
                        else: sb[y, x] = ((p - 8) << 12) | (hp << 8) | 0x200
    def mix(prio):
        s = sb[:H, :W]
        m = (s != 0) & (((s >> 8) & 1) == prio)
        sh = (s >> 9) & 1
        pend = s & 0xff; lvl = (s >> 12) & 15
        a = m & (sh == 0); bmp[a] = pend[a] + 0x200
        b = m & (sh == 1) & (pend != 0); bmp[b] = pend[b] + 0x200 + (lvl[b] << 10)
        c = m & (sh == 1) & (pend == 0); bmp[c] = (bmp[c] & 0x3ff) + (lvl[c] << 10)
    if which in (None, 'SPR', 'SPRN', 'SPRE'):
        mix(0)
        if which is None: draw(L0, 1, lambda p: p >= 8)
        mix(1)
    img = P[bmp][16:248, 8:376]
    return img
if __name__ == '__main__':
    pre = sys.argv[1]
    outs = []
    for w in (None, 'L1', 'L0', 'SPRN', 'SPRE'):
        outs.append(render(pre, w))
    W = np.concatenate([np.concatenate(outs[:3], 1), np.concatenate(outs[3:] + [np.zeros_like(outs[0])], 1)], 0)
    Image.fromarray(W).save('/tmp/claude-0/wr.png')
