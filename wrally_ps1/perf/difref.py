"""Compara la imagen de la placa (referencia de perf/arcade.py, como MAME) con la que ensena la PS1 (capturas de
psxprof con DUMPAT) cuadro a cuadro, y atribuye cada pixel distinto a entradas de sprite y tiles.
uso: difref.py dir [cuadros a-b,c] [--png] [--placa] [--tol N] [--tolefx N] [-j N]
  dir     carpeta de psxprof con DUMPAT (dumps.csv, dump_NNNNN.ppm y .bin); informe en dir/difref.txt (con cuadros:
          difref_parcial.txt) y PNG en
          dir/difref/dif_NNNNN.png (--png: todos; si no, los cuadros con FALTA, SOBRA, PRIORIDAD, MODELO o SALTO):
          placa | PS1 | diferencias (rojo FALTA, azul SOBRA, magenta PRIORIDAD, amarillo COLOR, cian MODELO, naranja
          SALTO; en verde las zonas) | y el modelo de la PS1 si no coincide con la captura
  -j N    procesos (por defecto JOBS o 2)
  --placa en los cuadros que la PS1 no dibujo (FRAMESKIP) se compara con el estado de la placa de ese cuadro
          (dump_f_placa.bin): lo que se ve contra lo que ensenaria la placa (las diferencias son del salto: movimiento
          o paleta); si no, con el estado que se dibujo. El modelo de la PS1 (MODELO) usa siempre dump_f.bin
Ejemplo (Acropolis de noche, cuadros 1860-2400, estado del juego como el original y sin saltos de cuadro; CFX= para
lo que se juega; INPUTS elige el rally en 'Choose a Rally': 1180-1190:R Monte-Carlo, 1180-1190:L Lagos):
  CFX="-DOKI_CUADROS -DFRAMESKIP=0" perf/compilar_g.sh work/g
  gcc -O2 -w -o perf/psxprof perf/psxprof.c -lm && mkdir -p work/ref
  INPUTS=1170-1173:R,1179-1182:R,1188-1191:R DUMPAT=1860-2400 perf/psxprof ps1/wrps1_USA.cue work/g/wr2g.ps-exe \
    work/g/wr2g.nm 2400 work/ref
  python3 perf/difref.py work/ref
Alineacion: la placa ensena x 8..375, y 16..247 y la PS1 368x240 desde x 8, y 16: las 232 primeras filas.
Tolerancia: la PS1 pone a 5 bits las componentes de 4 bits de la placa sin perdida ((v << 1) | (v >> 3)): fuera de
los efectos se pide igualdad (--tol 0). Las sombras y luces (sprites de efecto: la placa cambia el banco de paleta de
lo que tapan) la PS1 las hace sumando y restando un color medio por banco (main.c eff_update): se admite --tolefx
(por defecto 6) en cada componente de 5 bits y se cuantifica el error (informe: efecto).
Clases de los pixeles distintos (y causa):
  FALTA      la placa pinta un sprite o tile (el de arriba) o aplica una sombra/luz y la PS1 no; (cache): su tile no
             estaba en la cache de VRAM al mandar la lista; (dibujo): estaba y no se dibujo ahi
  SOBRA      la PS1 pinta un sprite (o una sombra/luz) donde la placa no lo tiene
  PRIORIDAD  (orden) los dos tienen el sprite, tile o efecto en el pixel pero encima queda otra cosa
  COLOR      lo mismo encima y otro color: (efecto) sombra/luz de los dos fuera de tolerancia; (paleta) CLUT
  MODELO     el modelo de la PS1 (arcade.render_ps1) no coincide con la captura en ese pixel: la PS1 dibuja algo que
             el modelo no explica (no deberia pasar: avisa de un volcado mal emparejado o de algo nuevo en main.c)
  INDEF      MAME no define el pixel (capa 1 cat. 1, pen 0): no cuenta (no ha salido en ningun cuadro medido)
  SALTO      (--placa) cuadro que la PS1 no dibujo: se ve el anterior; lo distinto de la placa en ese cuadro"""
import sys, os, csv, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arcade as A
CLS = ['OK', 'FALTA', 'SOBRA', 'PRIORIDAD', 'COLOR', 'MODELO', 'INDEF', 'SALTO']
COL = {1: (255, 0, 0), 2: (0, 128, 255), 3: (255, 0, 255), 4: (255, 220, 0), 5: (0, 255, 255), 6: (90, 90, 90), 7: (255, 128, 0)}

def quien(src, idx, tile):
    if src == 3: return 'spr %d t%04x' % (idx, tile)
    if src == 5: return 'efecto spr %d t%04x' % (idx, tile)
    if src in (1, 2, 4): return 'capa%d (%d,%d) t%04x' % (1 if src in (1, 4) else 0, idx % 64, idx // 64, tile)
    return 'fondo'

def comparar(d, cap, tol=0, tolefx=6, dref=None):
    """d: volcado con el que se dibujo la captura cap (240, 368, 3, en 5 bits); dref: estado de la placa con que
    comparar (por defecto d; --placa en un cuadro saltado: el de ese cuadro). Devuelve la clase por pixel (232x368),
    la causa (1 cache, 2 dibujo, 3 efecto, 4 paleta, 5 orden), el culpable q (src, idx, tile; src 5 = sprite de
    efecto) y lo que hay encima en la otra imagen q2 (PRIORIDAD), la referencia, el modelo y datos para el informe"""
    dref = d if dref is None else dref
    r = A.render_placa(dref); m = A.render_ps1(d)
    H = A.HP; c = cap[:H].astype(np.int32)
    mod_ok = (m['r5'] == cap).all(-1)
    mdl = {k: v[:H] for k, v in m.items()}
    st_r, st_p = (x['spr'][3 + 3:3 + 511 * 4:4].astype(np.int32) & 0x3FFF for x in (dref, d))   # tile de cada entrada
    pres = d['pres']
    dl = np.abs(r['r5'] - c).max(-1)
    er, ep = r['efx'] >= 0, mdl['efx'] >= 0
    efx = (r['lvl'] > 0) | ep
    bad = dl > np.where(efx, tolefx, tol)
    rs = np.where(r['src'] == 4, 1, r['src']); ps = mdl['src']
    same = (rs == ps) & (r['idx'] == mdl['idx'])
    cl = np.zeros((H, A.W), np.uint8); causa = np.zeros((H, A.W), np.uint8)
    z = lambda: np.zeros((H, A.W), np.int32)
    q = [z(), z(), z()]; q2 = [z(), z(), z()]
    def pon(msk, k, ca, a, b=None):
        cl[msk] = k; causa[msk] = ca[msk] if np.ndim(ca) else ca
        for dst, src in ((q, a), (q2, b)):
            if src is None: continue
            for t in range(3): dst[t][msk] = src[t][msk] if np.ndim(src[t]) else src[t]
    own_r = (rs, r['idx'], r['tile']); own_p = (ps, mdl['idx'], mdl['tile'])
    miss = lambda t: ~pres[np.clip(t, 0, 16383)] & (t >= 0)
    # encima en la placa un sprite que la PS1 no ensena encima
    a = bad & ~same & (rs == 3)
    pr = a & (mdl['sprk'] == r['idx']); pon(pr, 3, 5, own_r, own_p)
    a &= ~pr; pon(a, 1, np.where(miss(r['tile']), 1, 2), own_r)
    # encima en la PS1 un sprite que la placa no ensena encima
    b = bad & ~same & (ps == 3) & (rs != 3)
    pr = b & (r['sprk'] == mdl['idx']); pon(pr, 3, 5, own_p, own_r)
    pon(b & ~pr, 2, 2, own_p)
    # capas distintas encima
    c_ = bad & ~same & (rs != 3) & (ps != 3)
    mr = c_ & miss(r['tile']); pon(mr, 1, 1, own_r)
    pon(c_ & ~mr, 3, 5, own_r, own_p)
    # lo mismo encima, otro color
    s_ = bad & same
    pon(s_ & er & ep, 4, 3, own_r, (5, mdl['efx'], st_p[np.clip(mdl['efx'], 0, 510)]))
    fe = s_ & er & ~ep                                         # sombra/luz de la placa que la PS1 no hace
    own_e = (5, r['efx'], st_r[np.clip(r['efx'], 0, 510)])
    mf = fe & miss(own_e[2]); pon(mf, 1, 1, own_e, own_r); pon(fe & ~mf, 3, 5, own_e, own_r)
    fe = s_ & ~er & ep                                         # sombra/luz de la PS1 que la placa no hace
    own_e = (5, mdl['efx'], st_p[np.clip(mdl['efx'], 0, 510)])
    pr = fe & (r['sefx'] >= 0); pon(pr, 3, 3, own_e, own_r); pon(fe & ~pr, 2, 3, own_e)
    pon(s_ & ~er & ~ep, 4, 4, own_r)
    if dref is not d: cl[bad] = 7                              # se ve el cuadro anterior: todo lo distinto es del salto
    mm = bad & ~mod_ok[:H]; cl[mm] = 5; causa[mm] = 0
    ind = r['src'] == 4; cl[ind] = np.where(bad[ind], 6, 0)
    return cl, causa, q, q2, r, mdl, dict(mod_px=int((~mod_ok).sum()), efx=efx & same & er & ep, c=c)

CAUSA = ['', 'cache', 'dibujo', 'efecto', 'paleta', 'orden']
def resumen(cl, causa, q, q2):
    """(clase, causa, culpable) -> pixeles"""
    out = collections.Counter(); ag = collections.Counter()
    m = (cl > 0) & (cl != 6)
    if not m.any(): return out, ag
    keep = (cl == 3) | (cl == 4)                               # lo de encima solo en PRIORIDAD y COLOR
    key = np.stack([a[m].astype(np.int64) for a in [cl, causa] + q] + [np.where(keep, a, 0)[m].astype(np.int64) for a in q2], 1)
    u, cnt = np.unique(key, axis=0, return_counts=True)
    for (k, ca, s, i, t, s2, i2, t2), v in zip(u.tolist(), cnt.tolist()):
        txt = quien(s, i, t) if k not in (5, 7) else ('modelo' if k == 5 else 'cuadro saltado: ' + quien(s, i, t))
        if s2: txt += {3: ' / encima en la otra: ', 4: ' con '}.get(k, ' sobre ') + quien(s2, i2, t2)
        out[(CLS[k], CAUSA[ca], txt)] += v
        ag[(CLS[k], CAUSA[ca], {3: 'sprite', 5: 'efecto', 1: 'capa1', 2: 'capa0'}.get(s, 'fondo'), t)] += v
    return out, ag

def zonas(cl):
    from scipy import ndimage
    lab, n = ndimage.label((cl > 0) & (cl != 6), structure=np.ones((3, 3)))
    return [(s[1].start, s[0].start, s[1].stop - s[1].start, s[0].stop - s[0].start) for s in ndimage.find_objects(lab)] if n else []

def png(fn, r, cap, mdl, cl, zs, mod_px):
    from PIL import Image, ImageDraw
    H = A.HP; capr = (cap[:H] << 3).astype(np.uint8)
    dif = (capr // 3).astype(np.uint8)
    for k, c in COL.items(): dif[cl == k] = c
    pan = [r['rgb'], capr, dif] + ([mdl['rgb'][:H]] if mod_px else [])
    sep = np.full((H, 4, 3), 255, np.uint8)
    img = Image.fromarray(np.concatenate(sum([[p, sep] for p in pan], [])[:-1], axis=1))
    dr = ImageDraw.Draw(img)
    for x, y, w, h in zs:
        for o in (0, A.W + 4, 2 * (A.W + 4)): dr.rectangle([o + x - 1, y - 1, o + x + w, y + h], outline=(0, 255, 0))
    img.save(fn, compress_level=1)

def uno(args):
    dr, f, g, sal, opt = args
    nm = os.path.join(dr, 'dump_%05d' % f)
    d = A.leer(nm + '.bin')
    dref = A.leer(nm + '_placa.bin') if opt['placa'] and sal and os.path.exists(nm + '_placa.bin') else None
    cap = A.leer_ppm(nm + '.ppm') >> 3
    cl, causa, q, q2, r, mdl, x = comparar(d, cap, opt['tol'], opt['tolefx'], dref)
    rs, ag = resumen(cl, causa, q, q2)
    n = collections.Counter(CLS[k] for k in cl.ravel() if k)
    zs = zonas(cl)
    # error de los efectos con el mismo dueno (lo que aproxima la PS1)
    ee = (x['c'] - r['r5'])[x['efx']]
    ok_dibujo = not (n['FALTA'] or n['SOBRA'] or n['PRIORIDAD'] or n['MODELO'] or n['SALTO'])
    if opt['png'] or not ok_dibujo:
        os.makedirs(os.path.join(dr, 'difref'), exist_ok=True)
        png(os.path.join(dr, 'difref', 'dif_%05d.png' % f), r, cap, mdl, cl, zs, x['mod_px'])
    lin = ['cuadro %d%s: %s; modelo PS1 %d px; tiles que faltan en la cache: capa %d, sprite %d; zonas %s' % (
        f, ' (saltado: se ve %d)' % g if sal else '', ', '.join('%s %d' % (k, n[k]) for k in CLS[1:] if n[k]) or 'igual',
        x['mod_px'], d['falta_capa'], d['falta_spr'], ' '.join('%dx%d@%d,%d' % (w, h, x0, y0) for x0, y0, w, h in zs[:8]))]
    for (k, c, q), v in sorted(rs.items(), key=lambda t: -t[1])[:12]:
        lin.append('   %-9s %-7s %5d px  %s' % (k, c, v, q))
    return f, lin, n, ee, ag

def main():
    a = sys.argv[1:]
    opt = dict(png='--png' in a, placa='--placa' in a, tol=0, tolefx=6, j=int(os.environ.get('JOBS', 2)))
    pos = []; i = 0
    while i < len(a):
        if a[i] in ('--tol', '--tolefx', '-j'): opt[a[i].lstrip('-')] = int(a[i + 1]); i += 2; continue
        if not a[i].startswith('--'): pos.append(a[i])
        i += 1
    if not pos: sys.exit(__doc__)
    dr = pos[0]; sel = None
    if len(pos) > 1:
        sel = set()
        for p in pos[1].split(','):
            u, _, v = p.partition('-'); sel.update(range(int(u), int(v or u) + 1))
    rows = [r for r in csv.DictReader(open(os.path.join(dr, 'dumps.csv'))) if sel is None or int(r['cuadro']) in sel]
    jobs = [(dr, int(r['cuadro']), int(r['dibujado']), int(r['saltado']), opt) for r in rows]
    if opt['j'] > 1 and len(jobs) > 4:
        from multiprocessing import Pool
        with Pool(opt['j']) as p: res = p.map(uno, jobs, chunksize=4)
    else: res = [uno(j) for j in jobs]
    tot = collections.Counter(); ee = []; out = []; agr = {}
    for (f, lin, n, e, ag), r in zip(res, rows):
        out += lin; tot.update(n); ee.append(e)
        for k, v in ag.items():
            a_ = agr.setdefault(k, [0, 0, f, f]); a_[0] += 1; a_[1] += v; a_[3] = f
        if r['huella_ok'] != '1': out.append('   AVISO: el estado cambio entre la entrada en rc_vblank y la lista')
        if r.get('imagen_ok', '1') != '1': out.append('   AVISO: se dibujo en el bufer despues de la lista (pantalla de carga): MODELO esperado')
    ee = np.concatenate(ee) if ee else np.zeros((0, 3))
    out.append('== %d cuadros (%d saltados); pixeles: %s' % (len(rows), sum(int(r['saltado']) for r in rows),
               ', '.join('%s %d' % (k, tot[k]) for k in CLS[1:] if tot[k]) or 'iguales'))
    out.append('   cuadros con FALTA %d, SOBRA %d, PRIORIDAD %d, COLOR %d, MODELO %d, SALTO %d' % tuple(
        sum(1 for _, _, n, _, _ in res if n[k]) for k in ('FALTA', 'SOBRA', 'PRIORIDAD', 'COLOR', 'MODELO', 'SALTO')))
    out.append('   por tile (clase, causa, que, tile: cuadros, pixeles, primer y ultimo cuadro), los que salen en mas cuadros:')
    for col in (False, True):                                  # primero FALTA, SOBRA, PRIORIDAD, MODELO; luego COLOR
        it = sorted(((k, v) for k, v in agr.items() if (k[0] == 'COLOR') == col), key=lambda x: (-x[1][0], -x[1][1]))
        for (k, c, w, t), (nf, px, f0, f1) in it[:12 if col else 40]:
            out.append('     %-9s %-7s %-6s %04x: %4d cuadros %8d px  %d-%d' % (k, c, w, t & 0xFFFF, nf, px, f0, f1))
    if len(ee):
        a_ = np.abs(ee).max(-1)
        out.append('   efecto (sombras/luces, mismo dueno, %d px): error max por componente de 5 bits: media %.2f, p50 %d, p90 %d, '
                   'p99 %d, max %d; con signo r %+.2f g %+.2f b %+.2f; %% <= 2: %.1f, <= 4: %.1f, <= 6: %.1f' % (
                   len(a_), a_.mean(), np.percentile(a_, 50), np.percentile(a_, 90), np.percentile(a_, 99), a_.max(),
                   *ee.mean(0), 100 * (a_ <= 2).mean(), 100 * (a_ <= 4).mean(), 100 * (a_ <= 6).mean()))
    txt = '\n'.join(out)
    open(os.path.join(dr, 'difref.txt' if sel is None else 'difref_parcial.txt'), 'w').write(txt + '\n')
    print(txt)

if __name__ == '__main__':
    main()
