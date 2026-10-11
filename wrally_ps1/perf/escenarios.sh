#!/bin/sh
# Escenarios de prueba de la cache de tiles, el CD y el sonido con psxprof, comparados con la linea base.
#   perf/escenarios.sh [escenario ...]     (por defecto todos: std seg att steer)
#   std    recorrido de medir.sh: moneda en 600, START, acelerar desde 1300 (3000 cuadros)
#   seg    segunda partida tras la primera, con direccion oscilante: STEER=1 COIN2=7200 (9500 cuadros)
#   att    attract largo (demostracion con su carrera) y luego moneda: NOCOIN1=1 COIN2=6000 (8000 cuadros)
#   steer  carrera con direccion oscilante (choques, derrapes): STEER=1 (4500 cuadros)
# Se compila la variante -DOKI_CUADROS -DFRAMESKIP=0 (estado del juego identico al del codigo original y un dibujo
# por cuadro; CFX la cambia) y se compara cuadro a cuadro con la linea base: BASE = carpeta wrally_ps1 de un
# checkout del commit f65069b ya construido (ps1/wrps1_USA.cue, ps1/wr2.ps-exe, ps1/wr2.elf o ps1/wr2.nm),
# ejecutada con el mismo psxprof. La base carga los tiles de forma sincrona: nunca le faltan, asi que con el
# mismo estado del juego toda imagen distinta es un tile que falta. Sin BASE solo se miran los contadores de
# tiles que faltan (psxprof mira tslot). Resultados en work/esc/<escenario>/ (ESCDIR la cambia; capturas cada 100
# cuadros, SHOT la cambia). Para medir lo que se juega de verdad (OKI en tiempo real, saltos de cuadro):
#   CFX= BASE= ESCDIR=work/esc_real perf/escenarios.sh
#   JOBS: escenarios a la vez (por defecto 2).  RAM_LD/ICF_BASE: modelo lento de psxprof.
set -e
cd "$(dirname "$0")/.."
R=$(pwd)
P=mipsel-linux-gnu
CFX="${CFX--DOKI_CUADROS -DFRAMESKIP=0}"; export CFX
ESC=${*:-std seg att steer}
O=${ESCDIR:-$R/work/esc}; mkdir -p $O; O=$(cd $O && pwd)
[ perf/psxprof -nt perf/psxprof.c ] || gcc -O2 -w -o perf/psxprof perf/psxprof.c -lm
perf/compilar_g.sh $O/g
if [ -n "$BASE" ]; then
  [ -f $BASE/ps1/wr2.nm ] || $P-nm -n $BASE/ps1/wr2.elf > $O/base.nm
  BNM=$BASE/ps1/wr2.nm; [ -f $BNM ] || BNM=$O/base.nm
fi
params() {
  case $1 in
  std) echo "3000" ;;
  seg) echo "9500 STEER=1 COIN2=7200" ;;
  att) echo "8000 NOCOIN1=1 COIN2=6000" ;;
  steer) echo "4500 STEER=1" ;;
  *) echo "escenario desconocido: $1" >&2; exit 1 ;;
  esac
}
run() {   # run nombre dir cue exe nm
  set -- $1 $2 $3 $4 $5 $(params $1)
  n=$1; d=$2; cue=$3; exe=$4; nm=$5; fr=$6; shift 6
  mkdir -p $d; rm -f $d/*.ppm
  env "$@" PROF_FROM=1000000 SHOT=${SHOT:-100} perf/psxprof $cue $exe $nm $fr $d > $d/psxprof.log 2>&1
}
J=0
for e in $ESC; do
  params $e > /dev/null
  run $e $O/$e ps1/wrps1_USA.cue $O/g/wr2g.ps-exe $O/g/wr2g.nm &
  J=$((J + 1)); [ $J -lt ${JOBS:-2} ] || { wait; J=0; }
  if [ -n "$BASE" ] && { [ ! -f $O/base_$e/cuadros.csv ] || [ perf/psxprof -nt $O/base_$e/cuadros.csv ]; }; then
    run $e $O/base_$e $BASE/ps1/wrps1_USA.cue $BASE/ps1/wr2.ps-exe $BNM &
    J=$((J + 1)); [ $J -lt ${JOBS:-2} ] || { wait; J=0; }
  fi
done
wait
for e in $ESC; do
  echo "== $e ($(params $e))"
  tail -1 $O/$e/psxprof.log
  if [ -n "$BASE" ]; then python3 perf/compara.py $O/$e $O/base_$e; else python3 perf/compara.py $O/$e; fi
done
