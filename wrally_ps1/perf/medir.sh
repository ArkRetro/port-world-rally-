#!/bin/sh
# Mide el EXE de PS1 con el perfilador psxprof (modelo de tiempos aproximado, constantes de DuckStation).
#   perf/medir.sh [cuadros] [desde]   (por defecto 3000 cuadros de juego, perfil desde el cuadro 2000 = carrera)
# Requiere haber construido antes (construir.sh). Compila una variante con -g (mismo codigo) para atribuir
# ciclos a lineas de C y a instrucciones del 68000. Deja los resultados en work/perf/.
# CFX: opciones de mas para ps1/*.c y rc/rc_mem.c (p. ej. CFX=-DFRAMESKIP=0 para comparar capturas pixel a pixel).
set -e
cd "$(dirname "$0")/.."
R=$(pwd)
N=${NUGGET:?define NUGGET con la ruta de nugget}
FR=${1:-3000}; FROM=${2:-2000}
OUT=$R/work/perf; mkdir -p $OUT/g
[ perf/psxprof -nt perf/psxprof.c ] || gcc -O2 -w -o perf/psxprof perf/psxprof.c -lm
P=mipsel-linux-gnu
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -g -O2 -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fno-strict-aliasing -fdata-sections -ffunction-sections -w -I$N"
B=$OUT/g
# objetos con -g (el de wr_rc.c solo si cambio)
[ $B/wr_rc.o -nt rc/wr_rc.c ] && [ $B/wr_rc.o -nt rc/rc_rt.h ] || $P-gcc $CF ${RCOPT:--Os} -c rc/wr_rc.c -o $B/wr_rc.o
for f in ps1/main ps1/cd ps1/snd; do $P-gcc $CF $CFX -c $f.c -o $B/$(basename $f).o; done
$P-gcc $CF -c snd/snd_tab.c -o $B/snd_tab.o
$P-gcc $CF -c snd/oki_tab.c -o $B/oki_tab.o
$P-gcc $CF -DMCS51_XOR=1 -c rc/mcu_rc.c -o $B/mcu_rc.o
$P-gcc $CF -DMCS51_XOR=1 -c rc/mcs51.c -o $B/mcs51.o
$P-gcc $CF -c rc/gcrypt.c -o $B/gcrypt.o
$P-gcc $CF $CFX -DRC_ROM_EXTERN -c rc/rc_mem.c -o $B/rc_mem.o
(cd ps1 && $P-gcc $ARCH -c data.s -o $B/data.o)
$P-gcc $ARCH -I$N -c $N/common/crt0/crt0.s -o $B/crt0.o
$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,--oformat=elf32-tradlittlemips -o $B/wr2g.elf \
  $B/crt0.o $B/main.o $B/wr_rc.o $B/mcs51.o $B/mcu_rc.o $B/gcrypt.o $B/rc_mem.o $B/cd.o $B/snd.o $B/snd_tab.o $B/oki_tab.o $B/data.o -lgcc
$P-objcopy -O binary $B/wr2g.elf $B/wr2g.ps-exe
$P-nm -n $B/wr2g.elf > $B/wr2g.nm
$P-objdump --dwarf=decodedline $B/wr2g.elf > $B/lineas.txt
# la imagen de CD que se mide es la construida (ps1/wrps1_USA.cue), con este EXE en memoria
rm -f $OUT/*.ppm
PROF_FROM=$FROM SHOT=${SHOT:-500} perf/psxprof ps1/wrps1_USA.cue $B/wr2g.ps-exe $B/wr2g.nm $FR $OUT 2>&1 | tail -2
python3 perf/resumen.py $OUT $FROM
python3 perf/analiza.py $OUT $B/wr2g.nm $B/lineas.txt rc/wr_rc.c ps1/main.c > $OUT/perfil.txt
head -24 $OUT/perfil.txt
