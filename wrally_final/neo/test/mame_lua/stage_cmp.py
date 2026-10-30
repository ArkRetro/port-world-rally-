"""Compara un tramo completo: original (368x232) contra port (320x224, reduccion 13/16).
uso: stage_cmp.py orig.gz port.gz outdir"""
import sys, gzip, struct, os, numpy as np
from PIL import Image
sys.path.insert(0, '/tmp/claude-0/s')
OW, OH, PW, PH = 368, 232, 320, 224
DY = 4
PV = [0,14,30,44,65,79,95,109,146,160,176,190,211,225,241,255]
FIX = np.zeros(256, np.uint8)
for v in range(256): FIX[v] = min(range(16), key=lambda i: abs(PV[i]-v))
def q4(a): return (a.astype(np.uint16) + 8) // 17     # original: niveles 0..15
def frames(fn, w, h):
    sz = 32 + w*h*4
    with gzip.open(fn) as f:
        while True:
            b = f.read(sz)
            if len(b) < sz: return
            hd = np.array(struct.unpack_from('<IHHHHH8H', b, 0), np.int64)
            a = np.frombuffer(b, np.uint8, w*h*4, 32).reshape(h, w, 4)
            yield hd, a[:, :, 2::-1]
def pack(a):  # 3x4 bits -> u16
    return (a[..., 0].astype(np.uint16) << 8) | (a[..., 1].astype(np.uint16) << 4) | a[..., 2]
def main(ofn, pfn, out):
    os.makedirs(out, exist_ok=True)
    # 1) original -> memmap 12 bits
    tmp = out + '/orig.u16'
    hs = []; mm = None; n = 0
    with open(tmp, 'wb') as f:
        for hd, a in frames(ofn, OW, OH):
            f.write(pack(q4(a)).tobytes()); hs.append(hd); n += 1
    HO = np.array(hs); O = np.memmap(tmp, np.uint16, 'r', shape=(n, OH, OW))
    print('orig frames', n, flush=True)
    xs = np.arange(PW); ub = (xs - 10) * 16.0 / 13.0 + 1.0
    rows = np.arange(PH)
    res = []; keep = []
    W = 1
    for pi, (hd, b) in enumerate(frames(pfn, PW, PH)):
        bq = pack(FIX[b])
        vy, vx = hd[2], hd[3]; wx, wy = hd[12], hd[13]
        dxs = (vx - HO[:, 3] + 512) % 1024 - 512; dys = (vy - HO[:, 2] + 256) % 512 - 256
        wdx = wx - HO[:, 12]; wdy = wy - HO[:, 13]
        cons = (np.abs(dxs - wdx) < 64) & (np.abs(dys - wdy) < 64) & (np.abs(dxs) < 200) & (np.abs(dys) < 150)
        cand = np.nonzero(cons)[0]
        if len(cand) == 0:
            res.append((pi, int(hd[0]), int(hd[1]), -1, 1.0, 0)); continue
        dist = np.abs(dxs[cand]) + np.abs(dys[cand])
        cand = cand[np.argsort(dist)[:4]]
        # busqueda fina del desfase en una rejilla (cada 3 px) alrededor del previsto
        gy = np.arange(44, PH, 3); gx = np.arange(14, 306, 3)
        Bs = bq[gy][:, gx]
        best = (-1, None, 0, 0)
        def evalo(oi, ex, ey):
            A = O[oi]
            yy = gy + DY + dys[oi] + ey
            vyy = (yy >= 40) & (yy < OH)
            if vyy.sum() < 10: return -1
            uu = np.round(ub[gx] + dxs[oi] + ex).astype(int)
            vv = (uu >= 0) & (uu < OW)
            S = A[np.clip(yy, 0, OH-1)][:, np.clip(uu, 0, OW-1)]
            m = vyy[:, None] & vv[None, :]
            return ((S == Bs) & m).sum() / max(1, m.sum())
        for oi in cand:
            for ey in range(-10, 11, 2):
                for ex in range(-12, 13, 2):
                    sc = evalo(oi, ex, ey)
                    if sc > best[0]: best = (sc, oi, ex, ey)
        if best[1] is not None:
            b0 = best
            for ey in (b0[3]-1, b0[3], b0[3]+1):
                for ex in (b0[2]-1, b0[2], b0[2]+1):
                    sc = evalo(b0[1], ex, ey)
                    if sc > best[0]: best = (sc, b0[1], ex, ey)
        _, oi, ex, ey = best
        if oi is None:
            res.append((pi, int(hd[0]), int(hd[1]), -1, 1.0, 0)); continue
        cand = [oi]
        ok = np.zeros((PH, PW), bool); valid = np.zeros((PH, PW), bool)
        A = O[oi]
        us = np.round(ub + dxs[oi] + ex).astype(int)
        for du in range(-W, W+1):
            uu = us + du; vu = (uu >= 0) & (uu < OW); uuc = np.clip(uu, 0, OW-1)
            yy = rows + DY + dys[oi] + ey; vy_ = (yy >= 40) & (yy < OH); yyc = np.clip(yy, 0, OH-1)
            S = A[yyc][:, uuc]
            v = vy_[:, None] & vu[None, :]
            ok |= (S == bq) & v; valid |= v
        # cuadros lisos (pantalla en negro/oliva del propio juego): sin comparar
        if (bq[44:] == bq[100, 160]).mean() > 0.9:
            res.append((pi, int(hd[0]), int(hd[1]), -2, 0.0, 0)); continue
        reg = valid.copy(); reg[:40] = False; reg[:, :12] = False; reg[:, 308:] = False
        bad = reg & ~ok
        frac = bad.sum() / max(1, reg.sum())
        # bloques 8x8 malos
        bb = bad[:PH//8*8, :PW//8*8].reshape(PH//8, 8, PW//8, 8).mean((1, 3)) > 0.5
        res.append((pi, int(hd[0]), int(hd[1]), int(oi), float(frac), int(bb.sum())))
        if bb.sum() >= 6:
            keep.append((int(bb.sum()), pi, int(oi), int(dxs[oi]+ex), int(dys[oi]+ey)))
            m = b.copy(); m[bad] = [255, 0, 255]
            Image.fromarray(m).save(f'{out}/p{pi:05d}.png')
        if pi % 500 == 0: print(pi, flush=True)
    np.save(out + '/res.npy', np.array(res, dtype=float))
    with open(out + '/keep.txt', 'w') as f:
        for k in keep: f.write(' '.join(map(str, k)) + '\n')
    # originales de los marcados (para comparar)
    for k in keep:
        oi = k[2]; a = O[oi]
        img = np.stack([((a >> 8) & 15) * 17, ((a >> 4) & 15) * 17, (a & 15) * 17], 2).astype(np.uint8)
        Image.fromarray(img).save(f'{out}/o{k[1]:05d}.png')
    del O; os.remove(tmp)
    print('done', len(res), 'flagged', len(keep))
if __name__ == '__main__': main(*sys.argv[1:4])
