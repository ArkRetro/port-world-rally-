#!/bin/sh
# Musica del titulo del WR2 para el menu: el sonido del WR2 sale del modelo del GAE1 (no hay pistas que copiar), asi
# que se saca lo que suena en el port: su imagen USA en psxprof2 (el perfilador del WR2, con el sonido de la SPU a WAV)
# durante el attract, hasta pasado el titulo, y musica.py corta la pieza. Sin emulador ni BIOS. Sale de las ROM: va a
# work/. Se puede poner otra en work/musica2.wav (mono o estereo, cualquier frecuencia: se usa entera).
set -e
cd "$(dirname "$0")"
R=$(pwd); mkdir -p work/mus/p2
W2=../wrally2_ps1
gcc -O2 -w -o $W2/perf/psxprof2 $W2/perf/psxprof2.c -lm
(cd $W2/ps1 && WAV="$R/work/mus/w2.wav" ../perf/psxprof2 wr2ps1_USA.cue w2ps1.ps-exe w2ps1.nm ${CUADROS:-2000} "$R/work/mus/p2" \
  > "$R/work/mus/p2/salida.txt" 2>&1)
rm -f work/mus/p2/prof.bin work/mus/p2/profn.bin
echo "sacado: work/mus/w2.wav (attract del WR2 en psxprof2)"
