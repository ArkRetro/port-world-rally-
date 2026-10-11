#!/bin/sh
# Perfil de regiones de memoria por instruccion del 68000: para cada acceso por registro, la region (RAM, ROM,
# paletas, sprites...) que toca en el banco de pruebas (attract y partida). rc68.py lo usa para generar una
# comprobacion de rango y el acceso directo en vez de pasar por la tabla de paginas; si la region no es la
# prevista se va al acceso general (siempre correcto: si falta o se queda viejo solo se pierde velocidad).
# Requiere construir.sh hecho. Escribe rc/regiones.txt; despues: python3 rc68.py wr_rc.c y recompilar.
set -e
cd "$(dirname "$0")"
B=../work/regiones; mkdir -p $B ../work/run
gcc -O1 -c -w -DMCS51_XOR=1 -Dmcs51_run=mcs51_run_sw -Dmcs51_reset=mcs51_reset_sw mcs51.c -o $B/mcs51_sw.o
gcc -O1 -c -w -DMCS51_XOR=1 mcu_rc.c -o $B/mcu_rc_sw.o
gcc -O1 -w -DRCREG -I. -o $B/rc_host_reg rc_host.c rc_mem.c ../snd/oki_tab.c wr_rc.c $B/mcu_rc_sw.o gcrypt.c $B/mcs51_sw.o
NOCOIN=1 RCREG=$B/attract.bin WR_WORK=../work $B/rc_host_reg 30000 ../work/run
RCREG=$B/moneda.bin WR_WORK=../work $B/rc_host_reg 8000 ../work/run
python3 - $B/attract.bin $B/moneda.bin > regiones.txt <<'EOF'
import sys, struct
m = [0] * 0x10000
for f in sys.argv[1:]:
    for i, v in enumerate(struct.unpack('<65536H', open(f, 'rb').read())): m[i] |= v
NOM = {1: 'rom', 2: 'ram', 4: 'vram', 8: 'pal', 16: 'spr', 32: 'vregs'}
def one(b, ok): return NOM[b] if b in NOM and NOM[b] in ok else '-'
print('# pc lectura escritura (region unica vista en el banco de pruebas; - = ninguna o varias). rc/regiones.sh')
for i, v in enumerate(m):
    r, w = one(v & 0xFF, ('rom', 'ram', 'vram', 'pal', 'spr', 'vregs')), one(v >> 8, ('ram', 'pal', 'spr', 'vregs'))
    if r != '-' or w != '-': print('%06x %s %s' % (i * 2, r, w))
EOF
wc -l regiones.txt
