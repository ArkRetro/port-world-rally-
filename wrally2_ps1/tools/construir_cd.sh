#!/bin/sh
# construir_cd.sh: los datos de video y de ROM del CD para el attract (hito H3) en work/cd/: ROM.BIN, ROMRES.BIN,
# MENUS.PAK, DEMO0-3.PAK, TILES.BIN y TILESA.BIN (doc/formatos.md), y work/romres.txt. Hace falta el banco compilado
# (banco.sh con PERFILES=1: rc/rc_host y rc/rc_host_rom) y work/gfx1.bin y work/prog.bin (tools/prep.py, gfx.py).
# 1. Corridas del banco (una a la vez; se saltan con SIN_CORRIDAS=1 si ya estan en work/attract y work/pt): el banco es
#    determinista desde el encendido, asi que una corrida larga del attract (80 000 cuadros = 8,5 ciclos de 9 389; los
#    ciclos cambian con el generador aleatorio) y la partida de Portugal (tools/inputs_pt.txt) seguida de 25 000 cuadros
#    mas (fin de partida, records y otros ciclos del attract con otra semilla). BLKLOG + LOG + DUMP (paletas) con
#    rc_host y ROMSTAT con rc_host_rom.
# 2. tilesbin.py --a (TILESA.BIN), rombank.py (ROM.BIN, ROMRES.BIN, romres.txt), paquetes.py (MENUS.PAK, DEMO0-3.PAK),
#    tilesbin.py --b (TILES.BIN) y verpak.py sobre todo.
set -e
cd "$(dirname "$0")/.."
W=$PWD/work; CD=$W/cd; T=$PWD/tools; mkdir -p "$CD"
T0=$(date +%s)
corrida() {  # nombre cuadros periodo_de_volcado
  mkdir -p "$W/$1/rom"
  (cd rc && WR2_WORK=$W BLKLOG=$W/$1/blk.bin LOG=1 DUMP=0-200000/$3 ./rc_host $2 "$W/$1" > "$W/$1/salida.txt" 2>&1)
  (cd rc && WR2_WORK=$W ROMSTAT=$W/$1/romstat.txt ./rc_host_rom $2 "$W/$1/rom" > "$W/$1/salida_rom.txt" 2>&1)
  echo "corrida $1: $2 cuadros, $(tail -1 "$W/$1/salida.txt")"
}
if [ -z "$SIN_CORRIDAS" ]; then
  INPUTS= corrida attract 80000 125
  INPUTS=$(cat "$T/inputs_pt.txt") corrida pt 36000 60
fi
T1=$(date +%s); echo "corridas: $((T1 - T0)) s"
BLK="$W/attract/blk.bin $W/pt/blk.bin"; DUMPS="$W/attract/dumps.bin $W/pt/dumps.bin"; RS="$W/attract/romstat.txt $W/pt/romstat.txt"
python3 "$T/tilesbin.py" --a "$CD/TILESA.BIN"
python3 "$T/rombank.py" $RS -o "$CD" --romres "$W/romres.txt"
python3 "$T/paquetes.py" --blk $BLK --dumps $DUMPS --romstat $RS -o "$CD"
python3 "$T/tilesbin.py" --b "$CD/TILES.BIN" --blk $BLK --dumps $DUMPS
T2=$(date +%s); echo "construccion: $((T2 - T1)) s"
python3 "$T/verpak.py" "$CD/MENUS.PAK" "$CD/DEMO0.PAK" "$CD/DEMO1.PAK" "$CD/DEMO2.PAK" "$CD/DEMO3.PAK" "$CD/ROMRES.BIN" "$CD/TILES.BIN" "$CD/TILESA.BIN"
ls -l "$CD"
echo "total: $(($(date +%s) - T0)) s"
