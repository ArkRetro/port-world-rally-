#!/bin/sh
# Corridas del banco con los coches L y R (FE8124 = 04B0 / 0320: el cursor de elegir coche a la izquierda o a la
# derecha antes de elegir) en los cuatro rallies, como las de corridas_tramos.sh (piloto automatico en varios estilos;
# 25_30 y 20_15 con salidas, choques y vuelcos), para las poses de cada coche en GAMEL/GAMER.PAK: con solo la partida de
# Portugal (ptL/ptR) faltaban muchas al girar fuerte o volcar en otros tramos (cuadrados en el coche). Sin ROMSTAT: los
# bancos de ROM de los tramos siguen saliendo de las corridas de N. Salida: work/coches/<coche>_<rally>_<estilo>/
# (blk.bin, log.txt, dumps.bin, inputs.txt). Uso: tools/corridas_coches.sh [cuadros (18000)] [estilos (los 8 de
# corridas_tramos.sh)] [coches ("L R")]; RALLIES ("0 1 2 3") como alli
set -e
W=$(cd "$(dirname "$0")/.." && pwd)/work
RC=$(cd "$(dirname "$0")/../rc" && pwd)
H=${RC_HOST:-$W/rc_host_p2}
N=${1:-18000}; ESTILOS=${2:-"10_150 10_90 15_150 8_200 12_120 10_60 25_30 20_15"}; COCHES=${3:-"L R"}
MENU_0="1200-1208:C,1300-1308:S,1450-1458:X,1800-1808:X"
MENU_1="1200-1208:C,1300-1308:S,1450-1458:X,1720-1728:R,1950-1958:X"
MENU_2="1200-1208:C,1300-1308:S,1450-1458:X,1720-1728:R,1790-1798:R,1950-1958:X"
MENU_3="1200-1208:C,1300-1308:S,1450-1458:X,1720-1728:R,1790-1798:R,1860-1868:R,1950-1958:X"
cd "$RC"
for c in $COCHES; do
  for r in ${RALLIES:-0 1 2 3}; do
    eval m=\$MENU_$r
    m="1380-1386:$c,$m"
    for e in $ESTILOS; do
      d=$W/coches/${c}_${r}_$e; mkdir -p $d
      [ -s $d/inputs.txt ] && { echo "$d hecho"; continue; }
      WR2_WORK=$W INPUTS="$m,2388-99999:X" AUTOP=2400-$N AP_STEP=${e%_*} AP_LOOK=${e#*_} BLKLOG=$d/blk.bin LOG=1 DUMP=0-99999/60 \
        $H $((N + 200)) $d > $d/salida.txt 2>&1
      grep "^INPUTS=" $d/salida.txt | sed 's/^INPUTS=//' > $d/piloto.txt
      echo "$m,2388-99999:X,$(cat $d/piloto.txt)" > $d/inputs.txt
      echo "$c $r $e: $(tail -n 1 $d/salida.txt) $(date +%T)"
    done
  done
done
echo FIN
