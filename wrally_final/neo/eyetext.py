"""Textos de la intro Neo Geo: 'MAX 330 MEGA' -> 'PORT ARKRETRO', 'PRO-GEAR SPEC' -> 'PRO BLOSTE SPEC'.
La BIOS pone en la capa fix unos numeros de tile fijos (dos filas de 8 px por linea de texto); aqui
se redibujan esos tiles. Las letras P R O S E A C salen de la propia linea 'PRO-GEAR SPEC' (de
eyecatch_fix.bin, del juego del usuario); T L B K estan dibujadas a mano en el mismo estilo.
El tile 0x0FF lo usa la BIOS como blanco en toda la pantalla: su hueco (columna 4, fila de arriba de la
linea 2) tiene que quedar vacio."""
L1T = [0x005, 0x007, 0x009, 0x00b, 0x00d, 0x00f, 0x015, 0x017, 0x019, 0x01b, 0x01d, 0x01f, 0x05e, 0x060, 0x07d]
L1B = [0x006, 0x008, 0x00a, 0x00c, 0x00e, 0x014, 0x016, 0x018, 0x01a, 0x01c, 0x01e, 0x040, 0x05f, 0x07c, 0x07e]
L2T = [0x07f, 0x09a, 0x09c, 0x09e, 0x0ff, 0x0bb, 0x0bd, 0x0bf, 0x0da, 0x0dc, 0x0de, 0x0fa, 0x0fc, 0x100, 0x102, 0x104, 0x106]
L2B = [0x099, 0x09b, 0x09d, 0x09f, 0x0ba, 0x0bc, 0x0be, 0x0d9, 0x0db, 0x0dd, 0x0df, 0x0fb, 0x0fd, 0x101, 0x103, 0x105, 0x107]

HAND = {   # filas 2-14 (13 filas), mismo trazo de 2 px y pens 2/3 de suavizado
    'T': ["11111111", "11111111", "...11...", "...11...", "...11...", "...11...", "...11...",
          "...11...", "...11...", "...11...", "...11...", "...11...", "...11..."],
    'L': ["11.....", "11.....", "11.....", "11.....", "11.....", "11.....", "11.....",
          "11.....", "11.....", "11.....", "11.....", "1111111", "1111111"],
    'B': ["1111123.", "11111113", "11...312", "11....21", "11...312", "1111113.", "1111113.",
          "11...312", "11....21", "11....21", "11...312", "11111113", "1111123."],
    'K': ["11....31", "11...312", "11..312.", "11.312..", "11312...", "11123...", "111113..",
          "1123213.", "112..21.", "11...312", "11....21", "11....31", "11....21"],
}

def _dec(b):
    px = [[0] * 8 for _ in range(8)]
    for blk, x0 in enumerate((4, 6, 0, 2)):
        for y in range(8):
            v = b[blk * 8 + y]; px[y][x0] = v & 15; px[y][x0 + 1] = v >> 4
    return px

def _enc(px):
    out = bytearray(32)
    for blk, x0 in enumerate((4, 6, 0, 2)):
        for y in range(8):
            out[blk * 8 + y] = (px[y][x0] & 15) | ((px[y][x0 + 1] & 15) << 4)
    return bytes(out)

def _line(s, top, bot):
    w = len(top) * 8
    a = [[0] * w for _ in range(16)]
    for c, (t, b) in enumerate(zip(top, bot)):
        pt, pb = _dec(s[t * 32:t * 32 + 32]), _dec(s[b * 32:b * 32 + 32])
        for y in range(8):
            for x in range(8):
                a[y][c * 8 + x] = pt[y][x]; a[8 + y][c * 8 + x] = pb[y][x]
    return a

def _glyphs(s):
    a = _line(s, L2T, L2B)
    g = lambda x0, x1: [row[x0:x1] for row in a[2:15]]
    G = {'P': g(1, 9), 'R': g(11, 19), 'O': g(22, 32), 'E': g(59, 66), 'A': g(68, 78), 'S': g(94, 103), 'C': g(125, 135)}
    for k, rows in HAND.items():
        G[k] = [[int(ch) if ch != '.' else 0 for ch in r] for r in rows]
    return G

def _draw(G, items, width):
    """items: lista de (x, letra)"""
    a = [[0] * width for _ in range(16)]
    for x0, ch in items:
        for y, row in enumerate(G[ch]):
            for x, v in enumerate(row):
                if v and 0 <= x0 + x < width:
                    a[2 + y][x0 + x] = v
    return a

def _layout(G, text, x, gap, space):
    items = []
    for ch in text:
        if ch == ' ':
            x += space; continue
        items.append((x, ch)); x += len(G[ch][0]) + gap
    return items, x - gap

def _store(s, a, top, bot):
    for c, (t, b) in enumerate(zip(top, bot)):
        pt = [row[c * 8:c * 8 + 8] for row in a[0:8]]
        pb = [row[c * 8:c * 8 + 8] for row in a[8:16]]
        if t == 0x0FF:
            assert not any(any(r) for r in pt), 'el tile 0x0FF (blanco de la BIOS) tiene que quedar vacio'
        else:
            s[t * 32:t * 32 + 32] = _enc(pt)
        s[b * 32:b * 32 + 32] = _enc(pb)

def patch(s):
    """s: bytearray de la S ROM con los tiles de la intro ya puestos"""
    if not any(s[0x09A * 32:0x09A * 32 + 32]):
        return          # sin eyecatch_fix.bin: no hay intro que cambiar
    G = _glyphs(s)
    # linea 1: 'PORT ARKRETRO', centrada en 15 columnas
    items, end = _layout(G, 'PORT ARKRETRO', 0, 1, 5)
    off = (120 - end) // 2
    _store(s, _draw(G, [(x + off, ch) for x, ch in items], 120), L1T, L1B)
    # linea 2: 'PRO BLOSTE SPEC', centrada. 'PRO' tiene que acabar en x<=32 y lo siguiente empezar en
    # x>=40 (el tile 0x0FF, arriba en x=32-39, es el blanco de la BIOS): el espacio cae justo ahi
    i1, e1 = _layout(G, 'PRO', 0, 1, 0)
    i1 = [(x + 32 - e1, ch) for x, ch in i1]
    i2, e2 = _layout(G, 'BLOSTE SPEC', 40, 1, 4)
    assert e2 <= 136
    i2 = i2
    _store(s, _draw(G, i1 + i2, 136), L2T, L2B)

if __name__ == '__main__':
    import nggfx
    s = bytearray(nggfx.font_srom())
    f = open('/home/claude/wrally/neo/build/eyecatch_fix.bin', 'rb').read()
    for i in range(0, len(f), 34):
        t = (f[i] << 8) | f[i + 1]; s[t * 32:t * 32 + 32] = f[i + 2:i + 34]
    patch(s)
    for top, bot, w in ((L1T, L1B, 120), (L2T, L2B, 136)):
        for row in _line(s, top, bot):
            print(''.join('.123456789ABCDEF'[v] for v in row))
        print()
