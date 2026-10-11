"""Imagen de referencia de la placa de World Rally desde un volcado de psxprof (DUMPAT), como la dibuja MAME
(wrally.cpp screen_update y gaelco_wrally_sprites.cpp), y aparte como la dibuja ps1/main.c (modelo de la PS1: el
mismo orden de cubos, variantes de paleta, cache de tiles y faros/sombras semitransparentes), para comparar.
Graficos: work/tiles.npy (tools/prep.py, sacado de las ROM del usuario).
uso: arcade.py dump_NNNNN.bin [salida.png|salida.npz] [--ps1]
  lista los sprites que se ven (y si su tile estaba en la cache); .png: la imagen; .npz: la imagen y por pixel quien
  lo pinta (los campos de render()). --ps1: el modelo de la PS1 en lugar de la placa
Validacion: con el volcado y la captura del mismo cuadro el modelo de la PS1 da la captura exacta (0 pixeles distintos
en todos los cuadros medidos, salvo los de la pantalla de carga), y la placa tambien salvo sombras/luces y lo que falta
en la cache: perf/difref.py lo comprueba en cada cuadro.

Volcado (little endian): cabecera de 16 u32 (magia 'WRD1', version, cuadro pedido f, cuadro dibujado g, banderas:
1 saltado (se ve g < f), 2 el estado cambio entre la entrada en rc_vblank y la lista, 4 estado de la placa de un
cuadro saltado (_placa.bin: la cache y la CLUT son las del cuadro dibujado, solo vale para la placa), 8 algo dibujo
en el bufer despues de la lista (pantalla de carga); vblank en que se ve, huella del estado al entrar, huella al mandar
la lista, tiles que faltan en capas y en sprites (psxprof count_missing), cuadro del bufer, llamada en que se ve);
VRAM de tiles 0x4000
(ya descifrada: rc_vw16 descifra al escribir; palabras del 68000 como u16 nativos), paleta 0x4000, sprites 0x1000,
vregs 0x10, tiles en la cache de VRAM al mandar la lista (bit t de 2048 bytes: tslot[t] != 0xFFFF) y la CLUT de la
VRAM de la PS1 en ese momento (filas 240-255 y x 0-287: variante v, paleta p en la fila 240 + v * 4 + p / 16, x =
p % 16 * 16; los efectos en la fila 240, x 256-271 lo que suman y 272-287 lo que restan).

Imagen: la placa ensena x 8..375, y 16..247 (368x232); la PS1 368x240 desde x 8, y 16 (las 8 filas de abajo no
salen en la placa). render() devuelve un dict con
  r5: (H, W, 3) componentes de 5 bits (la PS1 pone (v << 1) | (v >> 3) a las de 4 bits de la placa: sin perdida)
  rgb: (H, W, 3) 8 bits (placa: v * 17; PS1: v5 << 3, como las capturas de psxprof)
  src: quien pinta el pixel: 0 nada (fondo negro de la PS1), 1 capa 1 (fondo), 2 capa 0 (frente), 3 sprite,
       4 capa 1 con categoria 1 y pen 0 (MAME no lo dibuja: queda lo del cuadro anterior; aqui el color del pen 0)
  idx: celda del mapa (0-2047, fila * 64 + columna) o entrada de sprite (0-510); tile: numero de tile (0-16383)
  pen: pen (0-15); efx: entrada del sprite de efecto que cambia el color (-1 ninguno); lvl: banco de paleta 0-7 (placa)
  sprk: entrada del sprite normal que hay en el pixel aunque lo tape una capa (placa: mapa de sprites de MAME;
        PS1: el ultimo dibujado) o -1; sefx (placa): la del sprite de efecto en el mapa de sprites o -1
Prioridad (MAME): capa 1 opaca (solo categoria 0), capa 0 cat. 0 (pens 1-15), capa 1 cat. 1 (pens 1-15), capa 0
cat. 1 pens 1-7, sprites de prioridad 0, capa 0 cat. 1 pens 8-15, sprites de prioridad 1 (tile >= 0x3700, el truco
de MAME). Sprites: entradas 0-510 (palabras 3 + 4k), la ultima encima; x = ((x + px) & 0x3ff) - 0x0f; de efecto
(color bit 4): los pens 8-15 cambian el banco de paleta (pen - 8) de lo que hay debajo (o del sprite ya dibujado).
MAME no mezcla los sprites en la ultima fila y columna visibles (bucles con < bottom/right); aqui si (la PS1 tambien).
PS1 (main.c): fondo negro, A capa 1 entera (variante 0, pen 0 opaco), B capa 0 cat. 0 (v. 1), C capa 1 cat. 1 (v. 1),
D capa 0 cat. 1 pens 1-7 (v. 2), E00 sprites de efecto prio. 0 sumando, E01 restando, S0 sprites prio. 0 (v. 1),
F capa 0 cat. 1 pens 8-15 (v. 3), E10, E11, S1; lo que no esta en la cache no se dibuja."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
W, HP, HS = 368, 232, 240           # ancho, filas de la placa y de la PS1
OFF_VRAM, OFF_PAL, OFF_SPR, OFF_VREGS = 64, 64 + 0x4000, 64 + 0x8000, 64 + 0x9000
OFF_PRES = OFF_VREGS + 0x10; OFF_CLUT = OFF_PRES + 2048; TAM = OFF_CLUT + 16 * 288 * 2
_tiles = None
def tiles():
    global _tiles
    if _tiles is None:
        from paths import work
        fn = os.environ.get('WR_TILES', work('tiles.npy'))
        if not os.path.exists(fn): sys.exit('falta %s: python3 tools/prep.py (con WR_ROM) lo saca de tus ROM' % fn)
        _tiles = np.load(fn)
    return _tiles

def leer(fn):
    b = np.fromfile(fn, np.uint8)
    if len(b) != TAM or b[:4].tobytes() != b'WRD1': raise ValueError('%s: no es un volcado WRD1' % fn)
    h = b[:64].view('<u4')
    d = dict(zip(('magia', 'version', 'cuadro', 'dibujado', 'banderas', 'vblank', 'huella', 'huella_lista', 'falta_capa',
                  'falta_spr', 'cuadro_bufer', 'llamada'), (int(x) for x in h[:12])))
    d['vram'] = b[OFF_VRAM:OFF_PAL].view('<u2'); d['pal'] = b[OFF_PAL:OFF_SPR].view('<u2')
    d['spr'] = b[OFF_SPR:OFF_VREGS].view('<u2'); d['vregs'] = b[OFF_VREGS:OFF_PRES].view('<u2')
    d['pres'] = np.unpackbits(b[OFF_PRES:OFF_CLUT], bitorder='little').astype(bool)
    d['clut'] = b[OFF_CLUT:TAM].view('<u2').reshape(16, 288)
    return d

def c4(v):
    """paleta xxxxBBBBRRRRGGGG -> (..., 3) componentes de 4 bits r, g, b"""
    v = np.asarray(v, np.int32)
    return np.stack([(v >> 4) & 15, v & 15, (v >> 8) & 15], -1)
def c15(v):
    """15 bits de la PS1 -> (..., 3) componentes de 5 bits"""
    v = np.asarray(v, np.int32)
    return np.stack([v & 31, (v >> 5) & 31, (v >> 10) & 31], -1)
def a5(c):
    """4 bits -> 5 bits como ps1/main.c c15()"""
    return (c << 1) | (c >> 3)

def capa(d, L, H):
    """capa L (0 frente, 1 fondo) en pantalla: pen, color, categoria, celda, tile, espejo por pixel"""
    vr = d['vregs'].astype(np.int32)
    sx, sy = (int(vr[1]) + 12, int(vr[0]) + 16) if L == 0 else (int(vr[3]) + 8, int(vr[2]) + 16)
    w = d['vram'][L * 4096:(L + 1) * 4096].reshape(2048, 2).astype(np.int32)
    code, attr = w[:, 0] & 0x3FFF, w[:, 1]
    ys = (np.arange(H) + sy) & 511; xs = (np.arange(W) + sx) & 1023
    cell = (ys[:, None] >> 4) * 64 + (xs[None, :] >> 4)
    a = attr[cell]
    ty = np.where((a >> 7) & 1, 15 - (ys[:, None] & 15), ys[:, None] & 15)
    tx = np.where((a >> 6) & 1, 15 - (xs[None, :] & 15), xs[None, :] & 15)
    t = code[cell]
    return dict(pen=tiles()[t, ty, tx].astype(np.int32), col=a & 31, cat=(a >> 5) & 1, cell=cell, tile=t)

def lista_sprites(d, H):
    """entradas visibles: (k, ys, xs, pens 16x16 con espejo, color, efecto, prio, tile)"""
    e = d['spr'][3:3 + 511 * 4].reshape(511, 4).astype(np.int32)
    T = tiles(); out = []
    px = np.arange(16)
    for k in range(511):
        w0, w1, w2, w3 = e[k]
        sx = w2 & 0x3FF; sy = (240 - (w0 & 0xFF)) & 0xFF
        X = ((((sx + px) & 0x3FF) - 0x0F) & 0x3FF) - 8; Y = ((sy + px) & 0x1FF) - 16
        vx = (X >= 0) & (X < W); vy = (Y >= 0) & (Y < H)
        if not vx.any() or not vy.any(): continue
        num = w3 & 0x3FFF; g = T[num]
        if (w0 >> 15) & 1: g = g[::-1]
        if (w0 >> 14) & 1: g = g[:, ::-1]
        out.append((k, Y[vy], X[vx], g[np.ix_(vy, vx)].astype(np.int32), (w2 >> 10) & 15, (w2 >> 14) & 1, int(num >= 0x3700), num))
    return out

def _nuevo(H):
    return dict(src=np.zeros((H, W), np.uint8), idx=np.full((H, W), -1, np.int32), tile=np.full((H, W), -1, np.int32),
                pen=np.zeros((H, W), np.int32), efx=np.full((H, W), -1, np.int32), lvl=np.zeros((H, W), np.int32),
                sprk=np.full((H, W), -1, np.int32))
def _pon(o, m, src, idx, tile, pen):
    o['src'][m] = src; o['idx'][m] = idx[m] if np.ndim(idx) else idx; o['tile'][m] = tile[m] if np.ndim(tile) else tile
    o['pen'][m] = pen[m] if np.ndim(pen) else pen; o['efx'][m] = -1; o['lvl'][m] = 0

def render_placa(d, H=HP):
    """como MAME: indice de color (banco << 10 | paleta * 16 + pen) por pixel y de ahi la paleta"""
    c1, c0 = capa(d, 1, H), capa(d, 0, H)
    o = _nuevo(H); bm = np.zeros((H, W), np.int32)
    def lay(c, m, src):
        bm[m] = (c['col'] * 16 + c['pen'])[m]; _pon(o, m, src, c['cell'], c['tile'], c['pen'])
    m = c1['cat'] == 0; lay(c1, m, 1)
    m = c1['cat'] == 1; lay(c1, m, 4)                              # MAME: sin dibujar (lo de antes)
    lay(c0, (c0['cat'] == 0) & (c0['pen'] > 0), 2)
    lay(c1, (c1['cat'] == 1) & (c1['pen'] > 0), 1)
    lay(c0, (c0['cat'] == 1) & (c0['pen'] > 0) & (c0['pen'] < 8), 2)
    # mapa de sprites (m_temp_bitmap_sprites): ssss --ez PPPP pppp
    tmp = np.zeros((H, W), np.int32); tk = np.full((H, W), -1, np.int32); te = np.full((H, W), -1, np.int32)
    tt = np.full((H, W), -1, np.int32)
    for k, ys, xs, g, col, ef, hp, num in lista_sprites(d, H):
        sub = tmp[np.ix_(ys, xs)]
        if not ef:
            m = g != 0
            sub[m] = (g | (col << 4) | (hp << 8))[m]
            tmp[np.ix_(ys, xs)] = sub
            for a, v in ((tk, k), (tt, num), (te, -1)):
                s = a[np.ix_(ys, xs)]; s[m] = v; a[np.ix_(ys, xs)] = s
        else:
            m = g >= 8
            nv = np.where(sub != 0, sub | ((g - 8) << 12) | 0x200, ((g - 8) << 12) | (hp << 8) | 0x200)
            sub[m] = nv[m]; tmp[np.ix_(ys, xs)] = sub
            s = te[np.ix_(ys, xs)]; s[m] = k; te[np.ix_(ys, xs)] = s
    o['sprk'] = tk.copy(); o['sefx'] = te.copy()
    def mix(p):
        m = (tmp != 0) & (((tmp >> 8) & 1) == p)
        sh = (tmp & 0x200) != 0; pend = tmp & 0xFF; lv = tmp >> 12
        a = m & ~sh
        bm[a] = pend[a] + 0x200; _pon(o, a, 3, tk, tt, pend & 15)
        b = m & sh & (pend != 0)
        bm[b] = pend[b] + 0x200 + (lv[b] << 10); _pon(o, b, 3, tk, tt, pend & 15)
        o['efx'][b] = te[b]; o['lvl'][b] = lv[b]
        c = m & sh & (pend == 0)
        bm[c] = (bm[c] & 0x3FF) + (lv[c] << 10); o['efx'][c] = te[c]; o['lvl'][c] = lv[c]
    mix(0)
    lay(c0, (c0['cat'] == 1) & (c0['pen'] >= 8), 2)
    mix(1)
    c = c4(d['pal'][bm & 0x1FFF])
    o['r5'] = a5(c); o['rgb'] = (c * 17).astype(np.uint8); o['bm'] = bm
    return o

def render_ps1(d, H=HS, cache=True):
    """como ps1/main.c con la CLUT que tenia la VRAM de la PS1 y los tiles que estaban en la cache"""
    c1, c0 = capa(d, 1, H), capa(d, 0, H)
    CL = d['clut'][:, :256].reshape(4, 4, 16, 16).reshape(4, 64, 16).astype(np.int32)   # [variante, paleta, pen]
    EF = d['clut'][0, 256:288].astype(np.int32)
    pres = d['pres'] if cache else np.ones(16384, bool)
    o = _nuevo(H); fb = np.zeros((H, W), np.int32)
    def lay(c, sel, v, src):
        t = CL[v, c['col'], c['pen']]
        m = sel & (t != 0) & pres[c['tile']]
        fb[m] = t[m]; _pon(o, m, src, c['cell'], c['tile'], c['pen'])
    lay(c1, np.ones((H, W), bool), 0, 1)                                # A
    lay(c0, c0['cat'] == 0, 1, 2)                                       # B
    lay(c1, c1['cat'] == 1, 1, 1)                                       # C
    lay(c0, (c0['cat'] == 1) & (c0['pen'] < 8), 2, 2)                   # D
    spr = [s for s in lista_sprites(d, H) if (not cache) or pres[s[7]]]
    def efecto(prio):
        for paso in (0, 1):                                             # E*0 suma, E*1 resta
            for k, ys, xs, g, col, ef, hp, num in spr:
                if not ef or hp != prio: continue
                t = EF[paso * 16 + g]; m = t != 0
                if not m.any(): continue
                sub = fb[np.ix_(ys, xs)]; a = c15(sub); b = c15(t)
                r = np.clip(a + b if paso == 0 else a - b, 0, 31)
                nv = (sub & 0x8000) | r[..., 0] | (r[..., 1] << 5) | (r[..., 2] << 10)
                sub[m] = nv[m]; fb[np.ix_(ys, xs)] = sub
                s = o['efx'][np.ix_(ys, xs)]; s[m] = k; o['efx'][np.ix_(ys, xs)] = s
    def normales(prio):
        for k, ys, xs, g, col, ef, hp, num in spr:
            if ef or hp != prio: continue
            t = CL[1, 32 + col, g]; m = t != 0
            ix = np.ix_(ys, xs)
            sub = fb[ix]; sub[m] = t[m]; fb[ix] = sub
            for a, v in (('src', 3), ('idx', k), ('tile', num), ('efx', -1), ('lvl', 0), ('sprk', k)):
                s = o[a][ix]; s[m] = v; o[a][ix] = s
            s = o['pen'][ix]; s[m] = g[m]; o['pen'][ix] = s
    efecto(0); normales(0)
    lay(c0, (c0['cat'] == 1) & (c0['pen'] >= 8), 3, 2)                  # F
    efecto(1); normales(1)
    c = c15(fb)
    o['r5'] = c; o['rgb'] = (c << 3).astype(np.uint8); o['fb'] = fb
    return o

def render(d, modo='placa', H=None):
    return render_placa(d, H or HP) if modo == 'placa' else render_ps1(d, H or HS)

def leer_ppm(fn):
    b = open(fn, 'rb').read(); p = b[:32].split()
    w, h = int(p[1]), int(p[2])
    return np.frombuffer(b[-w * h * 3:], np.uint8).reshape(h, w, 3)

def visibles(d, H=HP):
    """sprites visibles: (k, x, y, tile, color, efecto, prio, en cache)"""
    return [(k, int(xs[0]), int(ys[0]), num, col, ef, hp, bool(d['pres'][num])) for k, ys, xs, g, col, ef, hp, num in lista_sprites(d, H)]

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if not a: sys.exit(__doc__)
    d = leer(a[0]); modo = 'ps1' if '--ps1' in sys.argv else 'placa'
    o = render(d, modo)
    if len(a) > 1 and a[1].endswith('.npz'):
        np.savez_compressed(a[1], **{k: v for k, v in o.items() if k in ('rgb', 'r5', 'src', 'idx', 'tile', 'pen', 'efx', 'lvl', 'sprk')})
    elif len(a) > 1:
        from PIL import Image
        Image.fromarray(o['rgb']).save(a[1])
    print('cuadro %d (dibujado %d, banderas %d): vregs %s, faltan %d tiles de capa y %d de sprite' % (
        d['cuadro'], d['dibujado'], d['banderas'], ' '.join('%04x' % v for v in d['vregs'][:4]), d['falta_capa'], d['falta_spr']))
    for k, x, y, num, col, ef, hp, pr in visibles(d):
        print('  sprite %3d x %4d y %4d tile %04x color %2d%s%s%s' % (k, x, y, num, col, ' efecto' if ef else '', ' prio1' if hp else '', '' if pr else ' FALTA'))
