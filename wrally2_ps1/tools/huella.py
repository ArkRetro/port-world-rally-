"""huella.py a.txt b.txt: compara dos huellas de estado por llamada a rc_vblank (rc_host HUELLA y psxprof2 HUELLA,
lineas "llamada hash"): llamadas comparadas, cuantas distintas y la primera; si no casan desde el principio, busca un
desfase constante de numeracion (b = a + d, |d| <= 8) y lo dice. Puerta G-ESTADO: 0 distintas. Sale con 1 si hay."""
import sys

def lee(fn):
    d = {}
    for l in open(fn):
        p = l.split()
        if len(p) >= 2: d[int(p[0])] = p[1]
    return d

def compara(a, b, d):
    com = [k for k in sorted(a) if k + d in b]
    mal = [k for k in com if a[k] != b[k + d]]
    return com, mal

a, b = lee(sys.argv[1]), lee(sys.argv[2])
com, mal = compara(a, b, 0)
mejor = 0
if mal and mal[0] < 10:                    # distintas desde el principio: quiza otra numeracion
    for d in range(-8, 9):
        c2, m2 = compara(a, b, d)
        if c2 and len(m2) < len(mal) - 0 and len(c2) >= len(com) // 2: mal, com, mejor = m2, c2, d
if mejor: print('desfase de numeracion: b = a + %d' % mejor)
print('llamadas comparadas %d (a: %d, b: %d), distintas %d%s' % (len(com), len(a), len(b), len(mal), (', primera %d' % mal[0]) if mal else ''))
if mal:
    print('primeras distintas:', ' '.join(str(k) for k in mal[:10]))
    sys.exit(1)
print('G-ESTADO: iguales')
