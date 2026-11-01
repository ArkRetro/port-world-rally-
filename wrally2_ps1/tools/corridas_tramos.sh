#!/bin/sh
# Corridas del banco para los paquetes de carrera (H4, diseno 10.1): por rally (0 Portugal, 1 Safari, 2 Corcega, 3 RAC)
# una partida entera con el piloto automatico (AUTOP) en varios estilos (AP_STEP/AP_LOOK distintos dan lineas
# distintas), con BLKLOG + LOG + DUMP; y despues la misma partida (el guion INPUTS que escribio el piloto) con ROMSTAT.
# Salida: work/tramos/<rally>_<estilo>/ (blk.bin, log.txt, dumps.bin, inputs.txt, romstat.txt). Las partidas que ya
# estan en tools/pilotos.txt se repiten con su guion (sin el piloto, que usa fork).
# Uso: tools/corridas_tramos.sh [cuadros (20000)] [estilos ("10_150 10_90 15_150 8_200 12_120 10_60 25_30 20_15")]
# RALLIES ("0 1 2 3"): los rallies que se corren (construir.sh lanza uno por proceso)
set -e
W=$(cd "$(dirname "$0")/.." && pwd)/work
RC=$(cd "$(dirname "$0")/../rc" && pwd)
H=${RC_HOST:-$W/rc_host_p2}; HR=${RC_HOST_ROM:-$W/rc_host_rom2}
N=${1:-20000}; ESTILOS=${2:-"10_150 10_90 15_150 8_200 12_120 10_60 25_30 20_15"}   # los dos ultimos: con salidas y choques
MENU_0="1200-1208:C,1300-1308:S,1450-1458:X,1800-1808:X"
MENU_1="1200-1208:C,1300-1308:S,1450-1458:X,1720-1728:R,1950-1958:X"
MENU_2="1200-1208:C,1300-1308:S,1450-1458:X,1720-1728:R,1790-1798:R,1950-1958:X"
MENU_3="1200-1208:C,1300-1308:S,1450-1458:X,1720-1728:R,1790-1798:R,1860-1868:R,1950-1958:X"
PIL=$(cd "$(dirname "$0")" && pwd)/pilotos.txt
cd "$RC"
for r in ${RALLIES:-0 1 2 3}; do
  eval m=\$MENU_$r
  for e in $ESTILOS; do
    d=$W/tramos/${r}_$e; mkdir -p $d
    [ -s $d/romstat.txt ] && { echo "$d hecho"; continue; }
    g=$(grep "^tramos/${r}_$e " "$PIL" | cut -d" " -f2-)
    if [ -n "$g" ]; then    # partida grabada (tools/pilotos.txt): la misma, sin el piloto
      echo "$g" > $d/inputs.txt
      WR2_WORK=$W INPUTS="$g" BLKLOG=$d/blk.bin LOG=1 DUMP=0-99999/60 $H $((N + 200)) $d > $d/salida.txt 2>&1
    else
      WR2_WORK=$W INPUTS="$m,2388-99999:X" AUTOP=2400-$N AP_STEP=${e%_*} AP_LOOK=${e#*_} BLKLOG=$d/blk.bin LOG=1 DUMP=0-99999/60 \
        $H $((N + 200)) $d > $d/salida.txt 2>&1
      grep "^INPUTS=" $d/salida.txt | sed 's/^INPUTS=//' > $d/piloto.txt
      echo "$m,2388-99999:X,$(cat $d/piloto.txt)" > $d/inputs.txt
    fi
    WR2_WORK=$W INPUTS="$(cat $d/inputs.txt)" ROMSTAT=$d/romstat.txt $HR $((N + 200)) $d/rom > $d/salida_rom.txt 2>&1
    echo "$r $e: $(tail -n 1 $d/salida.txt) $(date +%T)"
  done
done
echo FIN
