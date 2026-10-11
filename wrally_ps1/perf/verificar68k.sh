#!/bin/sh
# Compara el codigo 68000 recompilado con Musashi instruccion a instruccion (registros, banderas vivas, y RAM,
# VRAM, paletas y sprites en cada cuadro). Requiere construir.sh hecho y MUSASHI con la ruta de Musashi
# (https://github.com/kstenerud/Musashi). Uso: perf/verificar68k.sh [cuadros_attract] [cuadros_con_moneda]
set -e
cd "$(dirname "$0")/../rc"
M=${MUSASHI:?define MUSASHI con la ruta de Musashi}
B=../work/musashi; mkdir -p $B
if [ ! -f $B/m68kops.c ]; then
  cp $M/*.c $M/*.h $B/ && cp -r $M/softfloat $B/
  sed -i 's/#define M68K_EMULATE_INT_ACK        M68K_OPT_OFF/#define M68K_EMULATE_INT_ACK        M68K_OPT_ON/' $B/m68kconf.h
  printf '\nvoid m68k_check_irq_now(void) { m68ki_check_interrupts(); }\n' >> $B/m68kcpu.c
  (cd $B && gcc -O2 -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c >/dev/null)
  for f in m68kcpu m68kops m68kdasm; do gcc -O1 -w -I$B -c $B/$f.c -o $B/$f.o; done
  gcc -O1 -w -I$B -c $B/softfloat/softfloat.c -o $B/softfloat.o
fi
gcc -O1 -w -DRCDBG -I. -I$B -c wr_rc.c -o $B/wr_rc_dbg.o
gcc -O1 -c -w -DMCS51_XOR=1 -Dmcs51_run=mcs51_run_sw -Dmcs51_reset=mcs51_reset_sw mcs51.c -o $B/mcs51_sw.o
gcc -O1 -c -w -DMCS51_XOR=1 mcu_rc.c -o $B/mcu_rc_sw.o
gcc -O1 -c -w mcs51.c -o $B/mcs51_be.o
gcc -O1 -w -DRCDBG -I. -I$B -o $B/rc_host_dbg rc_host.c rc_mem.c ../snd/oki_tab.c $B/wr_rc_dbg.o $B/mcu_rc_sw.o gcrypt.c $B/mcs51_sw.o $B/mcs51_be.o $B/m68kcpu.o $B/m68kops.o $B/m68kdasm.o $B/softfloat.o -lm
mkdir -p ../work/run
echo "== attract (sin moneda)"; NOCOIN=1 WR_WORK=../work $B/rc_host_dbg ${1:-30000} ../work/run
echo "== con moneda y carrera"; WR_WORK=../work $B/rc_host_dbg ${2:-8000} ../work/run
