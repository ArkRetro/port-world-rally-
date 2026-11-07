"""Codigo del 68000 que se recompila: lo alcanzable con DIP Monitors = One y los modos 1-15 de la pantalla 1 (los
16-23 son servicio, test y fallo de tension, a los que no se llega con las entradas de servicio sueltas). La pantalla 2
se queda en su modo 2 (test de arranque) y su logica de carrera no corre (0x36D4 bne 0x380C siempre tomado). Si el juego
saltara fuera, rc_bad para con el PC (se anade la semilla y se recompila).
Entrada: work/prog.bin y tools/semillas.txt (las tablas de estados se leen de la ROM del usuario).
Salida: work/code_addrs.txt (lo que lee rc/rc68.py). Puerta G-ALC: contiene todas las direcciones ejecutadas en las
trazas de MAME del analisis (tools/trazadas.txt)."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work, WR1
sys.path.insert(0, os.path.join(WR1, 'rc'))
from dec68 import Dec
T = os.path.dirname(os.path.abspath(__file__))
D = open(work('prog.bin'), 'rb').read(); d = Dec(D)
semillas = [int(l.split('#')[0], 16) for l in open(os.path.join(T, 'semillas.txt')) if l.split('#')[0].strip()]
# tablas de estados de 23 entradas (FEC294, FEC296, FEC298, FEC29A: modo - 1 de cada pantalla y su submodo)
tab = {}
for nom, base in [('294', 0xDB3E2), ('296', 0xDB43E), ('298', 0xDB49A), ('29A', 0xDB4F6)]:
    tab[nom] = [struct.unpack('>I', D[base + 4 * i:base + 4 * i + 4])[0] for i in range(23)]
entradas = set(x for v in tab.values() for x in v)
otras = [s for s in semillas if s not in entradas]
FORZADO = {0x36D4: 'tomado'}

def alcance(sem):
    INS = {}; pend = list(sem)
    for v in range(64):
        x = struct.unpack('>I', D[v * 4:v * 4 + 4])[0] & 0xFFFFFF
        if 0x400 <= x < len(D) and not x & 1: pend.append(x)
    while pend:
        a = pend.pop()
        if a in INS or a >= len(D) or a & 1: continue
        i = d.dec(a); INS[a] = i; n = a + i.size; o = i.op
        if o == 'bra': pend.append(i.target)
        elif o in ('bcc', 'dbcc'): pend += [i.target] if FORZADO.get(a) == 'tomado' else [n, i.target]
        elif o == 'bsr': pend += [i.target, n]
        elif o in ('jsr', 'jmp'):
            if i.dst.m in ('absl', 'absw', 'pcd'): pend.append(i.dst.v)
            if o == 'jsr': pend.append(n)
        elif o not in ('rts', 'rte', 'rtr'): pend.append(n)
    return INS

sem = [tab['294'][k] for k in range(15) if tab['294'][k]] + [tab['298'][k] for k in range(15) if tab['298'][k]] + \
      [tab['296'][1], tab['29A'][1]] + otras
C = alcance(sem)
todo = sum(1 for l in open(work('code_todo.txt')) if l.strip()) if os.path.exists(work('code_todo.txt')) else 0
open(work('code_addrs.txt'), 'w').write(''.join('%06x\n' % a for a in sorted(C)))
traz = {int(l, 16) for l in open(os.path.join(T, 'trazadas.txt')) if l.strip()}
fuera = sorted(traz - set(C))
print('alcance15: %d instrucciones (%d bytes)%s; trazadas en MAME %d, fuera %d' % (len(C), sum(i.size for i in C.values()),
      ' de %d' % todo if todo else '', len(traz), len(fuera)))
if fuera: sys.exit('G-ALC: direcciones ejecutadas fuera del conjunto: ' + ' '.join('%06x' % a for a in fuera[:20]))
