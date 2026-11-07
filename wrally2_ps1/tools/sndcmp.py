"""sndcmp.py mame.log banco.txt [desfase]: puerta G-SNDLOG: compara las escrituras al GAE1 ("W cuadro ... palabra dato
mascara": las de MAME llevan tiempo y PC entre el cuadro y la palabra; las del banco, SNDW, no) en orden: numero de
escrituras, primera distinta (cuadro, palabra, dato) y cuantas van en un cuadro distinto (desfase constante si lo hay).
Sale con 1 si difieren."""
import sys

def lee(fn):
    r = []
    for l in open(fn):
        p = l.split()
        if p and p[0] == 'W': r.append((int(p[1]), int(p[-3], 16), int(p[-2], 16), int(p[-1], 16)))
    return r

a, b = lee(sys.argv[1]), lee(sys.argv[2])
d = int(sys.argv[3]) if len(sys.argv) > 3 else 0
n = min(len(a), len(b)); mal = dif = 0; prim = None
# el banco numera el cuadro desde la primera IRQ y la toma antes de las escrituras de la IRQ (el lua, tras el
# frame_done): las mismas escrituras salen con el cuadro del banco = MAME + 1 (o igual); el arranque (antes de la
# primera IRQ) va todo en el cuadro 0 del banco
for i in range(n):
    x, y = a[i], b[i]
    if x[1:] != y[1:]: mal += 1; prim = prim or (i, x, y)
    elif y[0] and not (0 <= y[0] - x[0] - d <= 1): dif += 1; prim = prim or (i, x, y)
print('escrituras: MAME %d, banco %d, comparadas %d; distintas (palabra/dato/mascara) %d, fuera del cuadro (+0/+1) %d' % (len(a), len(b), n, mal, dif))
if prim: print('primera diferencia: escritura %d, MAME (cuadro %d, palabra %02x, %04x/%04x), banco (cuadro %d, %02x, %04x/%04x)' % (prim[0], *prim[1], *prim[2]))
ok = not mal and not dif and len(a) == len(b)
print('G-SNDLOG: %s' % ('igual' if ok else 'DISTINTO'))
sys.exit(0 if ok else 1)
