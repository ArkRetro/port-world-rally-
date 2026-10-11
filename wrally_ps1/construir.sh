#!/bin/sh
# World Rally -> PlayStation: construccion completa desde tus ROM (uso personal) - arkretro
#   WR_ROM   carpeta con las ROM de MAME de wrally (por defecto ./rom)
#   NUGGET   ruta de nugget (pcsx-redux/src/mips), para crt0 y el enlazado
#   MKPSXISO ejecutable de mkpsxiso (por defecto el del PATH)
set -e
cd "$(dirname "$0")"
R=$(pwd)
echo "== 1. datos desde las ROM"
python3 tools/prep.py
echo "== 2. traduccion 68000 -> C y 8051 -> C"
(cd rc && python3 rc68.py wr_rc.c && python3 rc51c.py mcu_rc.c)
echo "== 3. tablas de sonido y codificador PS-ADPCM"
(cd snd && python3 okitab.py && gcc -O2 -o psxadpcm psxadpcm.c)
echo "== 4. banco de pruebas en el PC y registro de tiles usados (para los paquetes de cada tramo)"
(cd rc && gcc -O1 -c -w -DMCS51_XOR=1 -Dmcs51_run=mcs51_run_sw -Dmcs51_reset=mcs51_reset_sw mcs51.c -o mcs51_sw.o \
  && gcc -O1 -c -w -DMCS51_XOR=1 mcu_rc.c -o mcu_rc_sw.o \
  && gcc -O1 -w -DBLKLOG -I. -o rc_host rc_host.c rc_mem.c ../snd/oki_tab.c wr_rc.c mcu_rc_sw.o gcrypt.c mcs51_sw.o \
  && mkdir -p ../work/run \
  && NOCOIN=1 WR_WORK=../work BLKLOG=../work/tl_nc.bin ./rc_host 30000 ../work/run \
  && WR_WORK=../work BLKLOG=../work/tl_co.bin ./rc_host 8000 ../work/run)
echo "== 5. graficos y sonido para el CD"
(cd ps1 && python3 gen2.py)
(cd snd && python3 sndps1.py)
echo "== 6. ejecutable de PS1 e imagenes de CD"
(cd ps1 && RCOPT=${RCOPT:--Os} ./build.sh)
ls -l ps1/wrps1_*.bin ps1/wrps1_*.cue
