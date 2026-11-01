#!/bin/sh
# Portadas del menu: el titulo de cada port en psxprof (WR1, sin monedas: el titulo del attract) y psxprof2 (WR2), que
# son deterministas, en el vblank de la captura (V1, V2). Hacen falta las imagenes USA de los dos ports y sus
# perfiladores compilados (wrally_ps1/perf/psxprof, wrally2_ps1/perf/psxprof2). Salen de las ROM: van a work/.
set -e
cd "$(dirname "$0")"
R=$(pwd); mkdir -p work/cap1 work/cap2
V1=${V1:-1800}; V2=${V2:-1600}
${MIPS:-mipsel-linux-gnu}-nm -n ../wrally_ps1/ps1/wr2.elf > work/cap1/wr1.nm
(cd ../wrally_ps1 && NOCOIN1=1 SHOTV=$V1 perf/psxprof ps1/wrps1_USA.cue ps1/wr2.ps-exe "$R/work/cap1/wr1.nm" $((V1 + 200)) "$R/work/cap1" > "$R/work/cap1/salida.txt" 2>&1)
(cd ../wrally2_ps1 && SHOTV=$V2 perf/psxprof2 ps1/wr2ps1_USA.cue ps1/w2ps1.ps-exe ps1/w2ps1.nm $((V2 + 200)) "$R/work/cap2" > "$R/work/cap2/salida.txt" 2>&1)
cp work/cap1/vbl_$(printf %05d $V1).ppm work/portada1.ppm
cp work/cap2/vbl_$(printf %05d $V2).ppm work/portada2.ppm
echo "portadas: work/portada1.ppm (WR1, vblank $V1) y work/portada2.ppm (WR2, vblank $V2)"
