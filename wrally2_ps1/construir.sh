#!/bin/sh
# World Rally 2 -> PlayStation: construccion completa desde tus ROM (uso personal) - arkretro. Lo mismo que se fue
# haciendo por partes (banco.sh, corridas, construir_cd.sh, build.sh), en orden y desde cero:
#   WR2_ROM  carpeta o zip con las ROM de MAME de wrally2 (o wrally2a; por defecto ./rom)
#   NUGGET   ruta de nugget (pcsx-redux/src/mips), para el enlazado (ps-exe.ld)
#   MKPSXISO ejecutable de mkpsxiso (por defecto el del PATH)
#   LICENCIA tu BIOS (o LICENSEA.DAT): logo de PlayStation del arranque (como en el WR1)
# Tarda segun la maquina (las corridas son unas 100 partidas del banco de pruebas); lo que ya este en work/ (corridas
# con su romstat.txt o inputs.txt) no se repite: si se corta, se puede volver a lanzar.
set -e
cd "$(dirname "$0")"
: "${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}"
T0=$(date +%s)
echo "== 1. datos de las ROM, codigo del 68000 recompilado a C y banco de pruebas"
PERFILES=1 sh banco.sh
echo "== 2. sonido: trozos del GAE1 en PS-ADPCM (SPU.BIN)"
gcc -O2 -o snd/psxadpcm snd/psxadpcm.c
python3 tools/sndwr2.py
echo "== 3. corridas del banco por los cuatro rallies (piloto automatico, 8 estilos) con los tres coches"
export RC_HOST="$PWD/rc/rc_host" RC_HOST_ROM="$PWD/rc/rc_host_rom"
corre() {   # script: un proceso por rally (las partidas grabadas de tools/pilotos.txt; las que no esten, con el piloto)
  for r in 0 1 2 3; do RALLIES=$r sh "tools/$1" > "work/$1.$r.log" 2>&1 & done
  wait
  for r in 0 1 2 3; do grep -q . "work/$1.$r.log" && tail -n 1 "work/$1.$r.log"; done
}
mkdir -p work
corre corridas_tramos.sh
corre corridas_coches.sh
echo "== 4. paquetes del CD (menus, demos, tramos, coches, ROM por bancos)"
sh tools/construir_cd.sh
echo "== 5. perfil de regiones de memoria (attract, Portugal y un tramo de cada rally) para el codigo recompilado"
mkdir -p work/reg
(cd rc && WR2_WORK=../work RCREG=../work/reg/attract.bin ./rc_host_reg 20000 ../work/reg/run > /dev/null 2>&1 &&
  WR2_WORK=../work INPUTS="$(cat ../tools/inputs_pt.txt)" RCREG=../work/reg/pt.bin ./rc_host_reg 36000 ../work/reg/run > /dev/null 2>&1 &&
  for r in 0 1 2 3; do
    WR2_WORK=../work INPUTS="$(cat ../work/tramos/${r}_10_150/inputs.txt)" RCREG=../work/reg/t$r.bin ./rc_host_reg 20200 ../work/reg/run > /dev/null 2>&1 || exit 1
  done)
python3 rc/regiones.py work/reg/*.bin
python3 rc/rc68.py rc/wr2_rc.c
echo "== 6. ejecutable de PS1 e imagenes de CD"
(cd ps1 && sh build.sh)
ls -l ps1/wr2ps1_*.bin ps1/wr2ps1_*.cue
echo "total: $(( $(date +%s) - T0 )) s"
