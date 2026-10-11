"""Perfil de regiones de memoria por instruccion del 68000 (para rc68.py): junta los volcados de rc_host -DRCREG
(uno por recorrido del banco de pruebas) y escribe work/regiones.txt: para cada acceso por registro, la region (RAM,
ROM, paletas, sprites...) que toca. rc68.py genera con ello una comprobacion de rango y el acceso directo; si la region
no es la prevista se va al acceso general (siempre correcto: si falta o se queda viejo solo se pierde velocidad).
Sale de ejecutar tu ROM, asi que no se sube al repositorio. Uso: regiones.py volcado.bin [...]"""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work
m = [0] * 0x10000
for f in sys.argv[1:]:
    for i, v in enumerate(struct.unpack('<65536H', open(f, 'rb').read())): m[i] |= v
NOM = {1: 'rom', 2: 'ram', 4: 'vram', 8: 'pal', 16: 'spr', 32: 'vregs'}
def one(b, ok): b &= 0x7F; return NOM[b] if b in NOM and NOM[b] in ok else '-'
out = ['# pc lectura escritura alineada (region unica vista en el banco de pruebas, - = ninguna o varias; a = las',
       '# lecturas de 32 bits siempre alineadas a 4). rc/regiones.py']
for i, v in enumerate(m):
    r, w = one(v & 0xFF, ('rom', 'ram', 'vram', 'pal', 'spr', 'vregs')), one(v >> 8, ('ram', 'vram', 'pal', 'spr', 'vregs'))
    if r != '-' or w != '-': out.append('%06x %s %s %s' % (i * 2, r, w, '-' if v & 0x80 else 'a'))
open(work('regiones.txt'), 'w').write('\n'.join(out) + '\n')
print('regiones:', len(out) - 2, 'instrucciones con region prevista')
