"""Perfil de regiones de memoria por instruccion del 68000 (para rc68.py): junta los volcados de rc_host -DRCREG
(uno por recorrido del banco de pruebas) y escribe work/regiones.txt: para cada acceso por registro, la region (RAM,
ROM, VRAM, paleta...) que toca. rc68.py genera con ello una comprobacion de rango y el acceso directo; si la region
no es la prevista se va al acceso general (siempre correcto: si falta o se queda viejo solo se pierde velocidad).
Tambien work/vueltas.txt, las veces que se ejecuta cada instruccion (rc68.py elige con ellas los bucles rapidos).
Sale de ejecutar tu ROM, asi que no se sube al repositorio. Uso: regiones.py volcado.bin [...]"""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work
m = [0] * 0x10000; n = [0] * 0x10000; nf = [0] * 0x10000; bk = [0] * 0x10000; fr = 0
for f in sys.argv[1:]:
    d = open(f, 'rb').read()
    for i, v in enumerate(struct.unpack('<65536H', d[:0x20000])): m[i] |= v
    # detras, las veces que se ejecuto cada instruccion (en total y como mucho en un cuadro), los cuadros y los
    # saltos atras tomados de cada instruccion
    if len(d) >= 0xE0004:
        fr += struct.unpack('<I', d[0xA0000:0xA0004])[0]
        for i, v in enumerate(struct.unpack('<65536I', d[0xA0004:0xE0004])): bk[i] += v
        for i, v in enumerate(struct.unpack('<65536I', d[0x20000:0x60000])): n[i] += v
        for i, v in enumerate(struct.unpack('<65536I', d[0x60000:0xA0000])): nf[i] = max(nf[i], v)
NOM = {1: 'rom', 2: 'ram', 4: 'vram', 8: 'pal', 32: 'vregs'}   # rc_regbit de rc_rt.h
def one(b, ok): b &= 0x7F; return NOM[b] if b in NOM and NOM[b] in ok else '-'
out = ['# pc lectura escritura alineada (region unica vista en el banco de pruebas, - = ninguna o varias; a = las',
       '# lecturas de 32 bits siempre alineadas a 4). rc/regiones.py']
for i, v in enumerate(m):
    r, w = one(v & 0xFF, ('rom', 'ram', 'vram', 'pal', 'vregs')), one(v >> 8, ('ram', 'vram', 'pal', 'vregs'))
    if r != '-' or w != '-': out.append('%06x %s %s %s' % (i * 2, r, w, '-' if v & 0x80 else 'a'))
open(work('regiones.txt'), 'w').write('\n'.join(out) + '\n')
print('regiones:', len(out) - 2, 'instrucciones con region prevista')
# veces que se ejecuta cada instruccion en el banco de pruebas: rc68.py elige con ellas los bucles rapidos
open(work('vueltas.txt'), 'w').write('# cuadros %d\n# pc veces maximo_en_un_cuadro saltos_atras\n' % fr +
                                     ''.join('%06x %d %d %d\n' % (i * 2, v, nf[i], bk[i]) for i, v in enumerate(n) if v))
