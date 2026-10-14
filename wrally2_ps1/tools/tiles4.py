"""tiles4.py: registros B de 144 bytes (doc/formatos.md 2.2): el tile a 4 bpp con una receta (indice 1-15 -> pen) hecha
con la paleta con la que mas se ve. Si el tile tiene <= 15 pens distintos (sin el 0) la receta es exacta; si no, se
funden pens por cercania de color en esa paleta (distancia RGB de 5 bits: cada vez el par de menor error, el grupo
con menos pixeles pasa al color del otro; al final una mejora local por intercambio) hasta 15. Banderas (byte 141): bit 0 sprite, 1 celda de efecto (indice = pen, pens 1-15), 2 indice = pen (todos
los pens en 0-15), 3 media paleta alta (sin pixel transparente y pens en 17-31: indice = pen - 16, receta k -> k + 16;
la PS1 puede dibujarlo con las entradas 16-31 de la CLUT del juego). Las paletas vienen de los volcados DUMP del banco
(WR2D, bloque 0x210000). Uso: tiles4.py blk.bin [...] --dumps dumps.bin [...]: mide la calidad de los tiles vistos
(exactos, con perdida y PSNR mediano, diseno 4.2 espera >= 38 dB)."""
import sys, os, struct, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfx, tilesbin

SPRITE, EFECTO, IDXPEN, ALTA = 1, 2, 4, 8

_tiles = None
def tiles():
    """los 32 768 tiles como array (32768, 16, 16) de pens, desde gfx1.bin (gfx.py, 5 planos; MSB a la izquierda)"""
    global _tiles
    if _tiles is None:
        g = tilesbin.g(); _tiles = np.zeros((32768, 16, 16), np.uint8)
        for p in range(5):
            a = np.frombuffer(g, np.uint8, 32768 * 32, p * gfx.PLANO).reshape(32768, 2, 16, 1)
            _tiles |= (np.unpackbits(a, axis=3).transpose(0, 2, 1, 3).reshape(32768, 16, 16) << p).astype(np.uint8)
    return _tiles

def tile(t): return tiles()[t & 0x7FFF]

def rgb5(pal):
    """32 colores del juego -> (32, 3) de R, G, B de 5 bits"""
    v = np.asarray(pal, np.int64); return np.stack([(v >> 10) & 31, (v >> 5) & 31, v & 31], 1)

def receta(px, pal, efecto=False):
    """px (16, 16) pens, pal 32 colores del juego (o varias paletas: los estados que toma en pantalla, p. ej. las
    flechas de la carrera, que se animan; la fusion minimiza el error sumado en todos) -> (indices (16, 16), pens[15]
    del indice 1-15 (0 = libre), banderas, psnr (None si exacto; inf si solo se fundieron pens del mismo color)).
    Efecto: indice = pen"""
    cnt = np.bincount(px.ravel(), minlength=32); cnt[0] = 0
    pens = np.flatnonzero(cnt); fl = 0; psnr = None
    if len(pens) == 0 or pens.max() <= 15:
        mapa = np.arange(32); fl |= IDXPEN | (EFECTO if efecto else 0); rec = [k if cnt[k] else 0 for k in range(1, 16)]
    elif pens.min() >= 17 and cnt.sum() == 256:
        mapa = np.arange(32) - 16; fl |= ALTA; rec = [k + 16 if cnt[k + 16] else 0 for k in range(1, 16)]
    else:
        assert not efecto, 'celda de efecto con pens > 15'
        P = np.asarray(pal)
        if P.ndim == 1: P = P[None]
        D = sum(((c[:, None, :] - c[None, :, :]) ** 2).sum(2) for c in (rgb5(q) for q in P)) / len(P)
        dest = np.arange(32); act = list(pens); n = cnt.astype(np.int64).copy()
        while len(act) > 15:                        # fusion voraz: minimo error (distancia^2 x pixeles que cambian)
            a = np.array(act); cost = D[a][:, a] * np.minimum(n[a][:, None], n[a][None, :])
            cost[np.diag_indices(len(a))] = 1 << 40
            i, j = np.unravel_index(cost.argmin(), cost.shape); x, y = int(a[i]), int(a[j])
            if n[x] < n[y]: x, y = y, x                 # y (menos pixeles) pasa al color de x
            n[x] += n[y]; act.remove(y); dest[dest == y] = x
        if len(act) < len(pens):                    # mejora local: cambiar un representante por un pen fundido
            S = np.array(act); Q = np.array([p for p in pens if p not in act]); w = cnt[:, None, None]
            err = (cnt * D[:, S].min(1)).sum()
            while True:
                DS = D[:, S]; o = np.argsort(DS, 1)[:, :2]; r = np.arange(32)
                m = np.where(o[:, :1] == np.arange(len(S))[None, :], DS[r, o[:, 1]][:, None], DS[r, o[:, 0]][:, None])
                e = (w * np.minimum(m[:, :, None], D[:, Q][:, None, :])).sum(0)    # error si S[i] se cambia por Q[j]
                i, j = np.unravel_index(e.argmin(), e.shape)
                if e[i, j] >= err: break
                err = e[i, j]; S[i], Q[j] = Q[j], S[i]
            act = sorted(S.tolist()); a = np.array(act); dest = a[D[:, a].argmin(1)]; dest[a] = a
        mapa = np.zeros(32, np.int64)
        for k, p in enumerate(act): mapa[dest == p] = k + 1
        rec = [act[k] if k < len(act) else 0 for k in range(15)]
        err = int((D[np.arange(32), dest] * cnt).sum()); tot = 256 * 3    # PSNR sobre los 256 pixeles, 3 canales
        psnr = None if len(act) == len(pens) else 10 * math.log10(31 * 31 / (err / tot)) if err else float('inf')
    idx = np.where(px > 0, mapa[px], 0).astype(np.uint8)
    return idx, rec, fl, psnr

def empaqueta(t, idx, rec, c, fl, avance):
    """registro B de 144 bytes"""
    idx = np.asarray(idx, np.uint8).reshape(16, 8, 2)
    r = bytearray((idx[:, :, 0] | idx[:, :, 1] << 4).astype(np.uint8).tobytes()) + bytearray(16)
    struct.pack_into('<H', r, 128, t & 0xFFFF)
    v = 0
    for k in range(15): v |= (int(rec[k]) & 31) << (5 * k)
    r[130:140] = v.to_bytes(10, 'little'); r[140] = c; r[141] = fl; struct.pack_into('<H', r, 142, avance & 0xFFFF)
    return bytes(r)

def desempaqueta(r):
    """registro B -> (codigo, indices 16x16, receta[15], paleta, banderas, avance)"""
    idx = [[(r[y * 8 + x // 2] >> (4 if x & 1 else 0)) & 15 for x in range(16)] for y in range(16)]
    v = int.from_bytes(r[130:140], 'little'); rec = [(v >> (5 * k)) & 31 for k in range(15)]
    return struct.unpack_from('<H', r, 128)[0], idx, rec, r[140], r[141], struct.unpack_from('<H', r, 142)[0]

def reg_b(t, c, pal, fl=0, avance=0):
    """registro B del tile t con la paleta c (32 colores en pal) -> (registro, psnr)"""
    idx, rec, f, psnr = receta(tile(t), pal, bool(fl & EFECTO))
    return empaqueta(t, idx, rec, c, f | (fl & SPRITE), avance), psnr

CORRIDA = 1 << 24                               # cuadro + corrida x CORRIDA: cada corrida cuenta desde 0

class Paletas:
    """las 64 paletas (2048 colores) de cada volcado WR2D del banco, por cuadro; y la palabra FE8124 (coche elegido).
    Con varios ficheros (uno por corrida, en el mismo orden que los BLKLOG) el cuadro va desplazado CORRIDA x indice"""
    def __init__(self, fns):
        self.p = {}; self.coche = {}
        for i, fn in enumerate(fns):
            d = open(fn, 'rb').read(); o = 0
            while o < len(d):
                assert d[o:o + 4] == b'WR2D', (fn, o)
                fr, nb = struct.unpack('>II', d[o + 8:o + 16]); o += 24; fr += i * CORRIDA
                for _ in range(nb):
                    base, n = struct.unpack('>II', d[o:o + 8]); o += 8
                    if base == 0x210000: self.p[fr] = np.frombuffer(d[o:o + 0x1000], dtype='>u2').astype(np.int32)
                    if base == 0xFE0000: self.coche[fr] = struct.unpack_from('>H', d, o + 0x8124)[0]
                    o += n
        self.cuadros = np.array(sorted(self.p))
        v = np.stack([self.p[fr] for fr in self.cuadros.tolist()]).reshape(-1, 64, 32)
        self.brillo = (((v >> 10) & 31) + ((v >> 5) & 31) + (v & 31)).sum(2)      # (volcado, paleta): suma de RGB
    def elige(self, c, intervalos):
        """cuadro de un volcado con la paleta c que mas se repite dentro de los intervalos [a, b] (los fundidos y los
        destellos duran poco); a igualdad, la mas viva. Si ninguno cae dentro, el volcado mas cercano al primero"""
        m = np.zeros(len(self.cuadros), bool)
        for a, b in intervalos: m[np.searchsorted(self.cuadros, a):np.searchsorted(self.cuadros, b, 'right')] = True
        if not m.any(): m[min(np.searchsorted(self.cuadros, intervalos[0][0]), len(self.cuadros) - 1)] = True
        cnt = {}
        for i in np.flatnonzero(m).tolist():
            k = self.p[int(self.cuadros[i])][c * 32:c * 32 + 32].tobytes(); n, _, j = cnt.get(k, (0, 0, i))
            cnt[k] = (n + 1, int(self.brillo[j, c]), j)
        return int(self.cuadros[max(cnt.values())[2]])
    def paleta(self, fr, c): return self.p[fr][c * 32:c * 32 + 32].tolist()
    def estados(self, c, intervalos, n=32):
        """los estados distintos de la paleta c en los volcados dentro de los intervalos (los mas repetidos, como mucho n;
        sin los apagados de los fundidos: brillo < 1/4 del mas vivo)"""
        from collections import Counter
        m = np.zeros(len(self.cuadros), bool)
        for a, b in intervalos: m[np.searchsorted(self.cuadros, a):np.searchsorted(self.cuadros, b, 'right')] = True
        if not m.any(): return [self.paleta(self.elige(c, intervalos), c)]
        ix = np.flatnonzero(m); bmax = self.brillo[ix, c].max(); cnt = Counter()
        for i in ix.tolist():
            if self.brillo[i, c] * 4 >= bmax: cnt[tuple(self.paleta(int(self.cuadros[i]), c))] += 1
        return [list(k) for k, _ in cnt.most_common(n)]

def cuenta(fns):
    """estadistica por codigo de los BLKLOG: codigo -> [Counter de color, banderas (SPRITE/EFECTO), primer cuadro,
    ultimo cuadro, intervalos de cuadros [(a, b)] en que se ve]; cuadros con la corrida como en Paletas. Con numpy: decenas de millones de entradas"""
    from collections import Counter, defaultdict
    st = defaultdict(lambda: [Counter(), 0, 1 << 30, -1, []])
    for i, fn in enumerate(fns):
        w = np.fromfile(fn, dtype='<u4'); p = 0; frs = []; ents = []
        while p + 5 <= len(w):
            n = int(w[p + 4] >> 16); e = w[p + 5:p + 5 + n]; e = e[(e >> 28) < 3]
            frs.append(np.full(len(e), int(w[p] & 0x7FFFFFFF) + i * CORRIDA, dtype=np.int64)); ents.append(e); p += 5 + n
        e = np.concatenate(ents); fr = np.concatenate(frs)
        key = (e & 0xFFFF).astype(np.int64) << 16 | (e >> 16) & 0x7F | ((e >> 28) & 3) << 8
        u, inv, cn = np.unique(key, return_inverse=True, return_counts=True)
        mn = np.full(len(u), 1 << 30, dtype=np.int64); mx = np.full(len(u), -1, dtype=np.int64)
        np.minimum.at(mn, inv, fr); np.maximum.at(mx, inv, fr)
        for k, c, a, b in zip(u.tolist(), cn.tolist(), mn.tolist(), mx.tolist()):
            s = st[k >> 16]; cl = (k >> 8) & 3
            s[0][k & 0x7F] += c; s[1] |= SPRITE if cl == 1 else EFECTO | SPRITE if cl == 2 else 0
            s[2] = min(s[2], a); s[3] = max(s[3], b); s[4].append((a, b))
    return st

def main():
    a = sys.argv[1:]
    if not a or '--dumps' not in a: print(__doc__); return
    i = a.index('--dumps'); blks, dumps = a[1:i], a[i + 1:]
    st = cuenta(blks); pal = Paletas(dumps)
    ex = col = 0; ps = {0: [], SPRITE: []}; n = 0
    for t, (cn, fl, a0, b0, iv) in sorted(st.items()):
        c = cn.most_common(1)[0][0]; fr = pal.elige(c, iv)
        r, psnr = reg_b(t, c, pal.paleta(fr, c), fl); n += 1
        if psnr is None: ex += 1
        elif psnr == float('inf'): col += 1
        else: ps[fl & SPRITE].append(psnr)
    todo = sorted(ps[0] + ps[SPRITE])
    print('tiles vistos %d: exactos %d (%.1f %%), exactos en color (pens repetidos en la paleta) %d, con perdida %d'
          % (n, ex, 100.0 * ex / n, col, len(todo)))
    for k, v in ps.items():
        v.sort()
        if v: print('  %s con perdida %d: PSNR mediana %.1f dB, minimo %.1f, < 38 dB %.1f %%'
                    % ('sprites' if k else 'plano  ', len(v), v[len(v) // 2], v[0], 100.0 * sum(1 for x in v if x < 38) / len(v)))
    if todo: print('  todos: mediana %.1f dB' % todo[len(todo) // 2])

if __name__ == '__main__': main()
