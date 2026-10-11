#!/bin/sh
# Mide el EXE de PS1 con el perfilador psxprof (modelo de tiempos aproximado, constantes de DuckStation).
#   perf/medir.sh [cuadros] [desde]   (por defecto 3000 cuadros de juego, perfil desde el cuadro 2000 = carrera)
# Requiere haber construido antes (construir.sh). Compila una variante con -g (mismo codigo) para atribuir
# ciclos a lineas de C y a instrucciones del 68000. Deja los resultados en work/perf/.
# CFX: opciones de mas para ps1/*.c y rc/rc_mem.c (p. ej. CFX=-DFRAMESKIP=0 para comparar capturas pixel a pixel).
# Las variables de escenario de psxprof (STEER, NOCOIN1, COIN2) cambian la secuencia de botones.
set -e
cd "$(dirname "$0")/.."
R=$(pwd)
FR=${1:-3000}; FROM=${2:-2000}
OUT=$R/work/perf; mkdir -p $OUT/g
[ perf/psxprof -nt perf/psxprof.c ] || gcc -O2 -w -o perf/psxprof perf/psxprof.c -lm
B=$OUT/g
perf/compilar_g.sh $B
# la imagen de CD que se mide es la construida (ps1/wrps1_USA.cue), con este EXE en memoria
rm -f $OUT/*.ppm
PROF_FROM=$FROM SHOT=${SHOT:-500} perf/psxprof ps1/wrps1_USA.cue $B/wr2g.ps-exe $B/wr2g.nm $FR $OUT 2>&1 | tail -2
python3 perf/resumen.py $OUT $FROM
python3 perf/analiza.py $OUT $B/wr2g.nm $B/lineas.txt rc/wr_rc.c ps1/main.c > $OUT/perfil.txt
head -24 $OUT/perfil.txt
