"""Pantalla del menu (recopilatorio/menu.c) al estilo del World Rally: bandera de cuadros ondeando de fondo (como el
marco del titulo del WR1), ELIGE JUEGO en cursiva con el degradado amarillo-rojo y el borde negro (la letra de la
pantalla de CARGANDO del port del WR1), las dos portadas (capturas del titulo de cada port) con un marco rojo oscuro
como las fotos de 'Choose A Rally' (recortadas: sin el marco del WR1 ni los creditos del WR2), debajo de cada una su placa de rally (roja la del WR1, azul la del WR2) y abajo
PULSA START, que parpadea (va aparte).
Uso: python3 pantalla.py salida.bin [portada_wr1 portada_wr2] [vista.png]
Las portadas son capturas de 368 x 240 (ppm o png; captura.sh las saca de psxprof y psxprof2): salen de las ROM y no
van en el repositorio. Sin ellas (o si falta el fichero) el hueco lleva un 1 o un 2 grande sobre fondo azul.
Salida (halfwords de 15 bits de la PS1): cabecera de 24 (ancho y alto de la pantalla; x y ancho alto de la portada 1,
de la 2, del rotulo de PULSA START y del de CARGANDO; ceros), la pantalla y los dos rotulos."""
import sys, os, math, struct
from PIL import Image

W, H = 368, 240
TW, TH = 160, 104                       # portadas: 368 x 240 reducidas (el mismo aspecto)
P1 = (14, 64); P2 = (W - 14 - TW, 64)
ROT = (112, 210, 144, 16)               # PULSA START: x y ancho alto (ancho par: la DMA sube de dos en dos)
CARG = (84, 105, 200, 32)               # CARGANDO (sobre negro, mientras la BIOS lee el EXE del juego)

FONT = {  # 5 x 7, bit 4 a la izquierda (las de lglyph de main.c del WR1 y el resto en el mismo trazo)
    'A': (14, 17, 17, 31, 17, 17, 17), 'C': (14, 17, 16, 16, 16, 17, 14), 'D': (30, 17, 17, 17, 17, 17, 30),
    'E': (31, 16, 16, 30, 16, 16, 31), 'G': (14, 17, 16, 23, 17, 17, 15), 'I': (14, 4, 4, 4, 4, 4, 14),
    'J': (7, 2, 2, 2, 2, 18, 12), 'L': (16, 16, 16, 16, 16, 16, 31), 'N': (17, 25, 21, 19, 17, 17, 17), 'O': (14, 17, 17, 17, 17, 17, 14),
    'P': (30, 17, 17, 30, 16, 16, 16), 'R': (30, 17, 17, 30, 20, 18, 17), 'S': (15, 16, 16, 14, 1, 1, 30),
    'T': (31, 4, 4, 4, 4, 4, 4), 'U': (17, 17, 17, 17, 17, 17, 14), 'W': (17, 17, 17, 21, 21, 21, 10),
    'Y': (17, 17, 10, 4, 4, 4, 4), '1': (4, 12, 4, 4, 4, 4, 14), '2': (14, 17, 1, 2, 4, 8, 31),
    '3': (31, 2, 4, 2, 1, 17, 14), '5': (31, 16, 30, 1, 1, 17, 14), '9': (14, 17, 17, 15, 1, 2, 12), ' ': (0,) * 7}
GRAD = [(0xFF, 0xF0, 0x60), (0xFF, 0xD8, 0x40), (0xFF, 0xB8, 0x20), (0xFF, 0x98, 0x10), (0xFF, 0x70, 0x00),
        (0xF0, 0x48, 0x00), (0xD0, 0x20, 0x00)]          # amarillo a rojo, fila a fila (lgrad del WR1)
BLANCO = [(0xF8, 0xF8, 0xF8)] * 2 + [(0xF0, 0xF0, 0xF0), (0xE8, 0xE8, 0xE8), (0xE0, 0xE0, 0xE0), (0xD0, 0xD0, 0xD0),
                                      (0xC0, 0xC0, 0xC0)]


def ancho(t, sc, paso=0): return len(t) * (paso or 6 * sc) - sc + 7 * sc // 3


def texto(im, x, y, t, sc, col, borde=(0, 0, 0), paso=0):
    """como gp_text del WR1: cursiva (cada fila corrida sc/3 a la derecha por fila hacia arriba) y borde de 1 pixel"""
    px = im.load(); lim = im.size
    for pasada in (0, 1):
        for k, ch in enumerate(t):
            g = FONT[ch]
            for r in range(7):
                for c in range(5):
                    if not g[r] & (16 >> c): continue
                    x0 = x + k * (paso or 6 * sc) + c * sc + (6 - r) * sc // 3; y0 = y + r * sc
                    if pasada == 0: caja(px, x0 - 1, y0 - 1, sc + 3, sc + 2, borde, lim)
                    else: caja(px, x0, y0, sc + 1, sc, col[r], lim)


def caja(px, x, y, w, h, c, lim=(W, H)):
    for j in range(max(y, 0), min(y + h, lim[1])):
        for i in range(max(x, 0), min(x + w, lim[0])): px[i, j] = c


def bandera():
    """cuadros de 20 px que ondean (desplazamiento senoidal) con luces y sombras de la onda; 3 x 3 muestras por pixel"""
    im = Image.new('RGB', (W, H)); px = im.load()
    for y in range(H):
        for x in range(W):
            acc = [0, 0, 0]
            for sy in range(3):
                for sx in range(3):
                    fx, fy = x + sx / 3, y + sy / 3
                    fase = fx / 46 + fy / 71
                    u = fx + 7 * math.sin(fy / 29 + fx / 53)
                    v = fy + 9 * math.sin(fase * 2.1)
                    claro = (int(u // 20) + int(v // 20)) & 1
                    luz = 0.62 + 0.38 * (0.5 + 0.5 * math.cos(fase * 2.1))
                    base = (232, 232, 236) if claro else (34, 34, 40)
                    for i in range(3): acc[i] += base[i] * luz
            px[x, y] = tuple(int(a / 9) for a in acc)
    return im


def oscurece(im, x, y, w, h, f):
    px = im.load()
    for j in range(y, y + h):
        for i in range(x, x + w):
            r, g, b = px[i, j]; px[i, j] = (int(r * f), int(g * f), int(b * f))


RECORTE = {1: (24, 14, 344, 223),       # WR1: la foto del titulo sin el marco de cuadros
           2: (17, 22, 351, 240)}       # WR2: sin Insert Coin ni CREDITS (la fila de arriba); el mismo aspecto


def portada(fich, n):
    if fich and os.path.exists(fich):
        im = Image.open(fich).convert('RGB')
        if im.size == (W, H): im = im.crop(RECORTE[n])
        return im.resize((TW, TH), Image.LANCZOS)
    im = Image.new('RGB', (TW, TH), (16, 40, 120))
    texto(im, (TW - ancho(str(n), 8)) // 2, (TH - 56) // 2, str(n), 8, GRAD)
    return im


def placa(im, x, y, w, h, fondo, t):
    """placa de rally: borde blanco redondeado, fondo de color y el nombre en blanco"""
    px = im.load()
    caja(px, x + 2, y, w - 4, h, (250, 250, 250)); caja(px, x, y + 2, w, h - 4, (250, 250, 250))
    caja(px, x + 2, y + 2, w - 4, h - 4, fondo)
    texto(im, x + (w - ancho(t, 2, 11)) // 2, y + (h - 14) // 2, t, 2, BLANCO, paso=11)


def marco(im, x, y, w, h):
    px = im.load()
    caja(px, x - 3, y - 3, w + 6, h + 6, (0, 0, 0)); caja(px, x - 2, y - 2, w + 4, h + 4, (120, 16, 16))


def pixeles(im):
    d = im.tobytes()
    return [tuple(d[i:i + 3]) for i in range(0, len(d), 3)]


def c15(c):
    r, g, b = c
    v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
    return v or 0x0400                   # sin el 0 (negro de verdad en la PS1 sin transparencias, por si acaso)


def main():
    a = sys.argv[1:]
    sal = a[0]; f1 = a[1] if len(a) > 1 else None; f2 = a[2] if len(a) > 2 else None
    im = bandera()
    oscurece(im, 0, 8, W, 46, 0.35)                                   # banda del titulo
    texto(im, (W - ancho('ELIGE JUEGO', 4)) // 2, 17, 'ELIGE JUEGO', 4, GRAD)
    for (x, y), f, n, col, nom in ((P1, f1, 1, (176, 24, 24), 'WORLD RALLY'), (P2, f2, 2, (24, 48, 168), 'WORLD RALLY 2')):
        marco(im, x, y, TW, TH); im.paste(portada(f, n), (x, y))
        placa(im, x - 3, y + TH + 9, TW + 6, 24, col, nom)
    rx, ry, rw, rh = ROT
    oscurece(im, rx - 16, ry - 3, rw + 32, rh + 6, 0.35)
    rot = im.crop((rx, ry, rx + rw, ry + rh))
    texto(rot, (rw - ancho('PULSA START', 2)) // 2, 1, 'PULSA START', 2, GRAD)
    cx, cy, cw, ch = CARG
    carg = Image.new('RGB', (cw, ch))
    texto(carg, (cw - ancho('CARGANDO', 4)) // 2, 2, 'CARGANDO', 4, GRAD)
    hdr = [W, H, P1[0], P1[1], TW, TH, P2[0], P2[1], TW, TH, rx, ry, rw, rh, cx, cy, cw, ch] + [0] * 6
    out = struct.pack('<24H', *hdr)
    out += b''.join(struct.pack('<H', c15(p)) for p in pixeles(im))
    out += b''.join(struct.pack('<H', c15(p)) for p in pixeles(rot))
    out += b''.join(struct.pack('<H', c15(p)) for p in pixeles(carg))
    open(sal, 'wb').write(out)
    if len(a) > 3:
        v = im.copy(); v.paste(rot, (rx, ry)); v.save(a[3])
        v = Image.new('RGB', (W, H)); v.paste(carg, (cx, cy)); v.save(a[3][:-4] + '_carga.png')


main()
