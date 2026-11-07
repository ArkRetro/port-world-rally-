#!/bin/sh
# psxprof2 (perf/psxprof2.c): el perfilador del WR1 con la huella de estado y los tiempos por cuadro del WR2
cd "$(dirname "$0")" && gcc -O2 -w -o psxprof2 psxprof2.c -lm && echo psxprof2 listo
