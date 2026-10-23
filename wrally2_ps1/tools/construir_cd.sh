#!/bin/sh
# construir_cd.sh: los datos de video y de ROM del CD en work/cd/: ROM.BIN, ROMRES.BIN, MENUS.PAK, DEMO0-3.PAK,
# GAMEN.PAK, ST00-11.PAK, TILES.BIN y TILESA.BIN (doc/formatos.md), y work/romres.txt. Hace falta el banco compilado
# (banco.sh con PERFILES=1: rc/rc_host y rc/rc_host_rom) y work/gfx1.bin y work/prog.bin (tools/prep.py, gfx.py).
# 1. Corridas del banco (una a la vez; se saltan con SIN_CORRIDAS=1 si ya estan en work/attract y work/pt): el banco es
#    determinista desde el encendido, asi que una corrida larga del attract (80 000 cuadros = 8,5 ciclos de 9 389; los
#    ciclos cambian con el generador aleatorio) y la partida de Portugal (tools/inputs_pt.txt) seguida de 25 000 cuadros
#    mas (fin de partida, records y otros ciclos del attract con otra semilla). BLKLOG + LOG + DUMP (paletas) con
#    rc_host y ROMSTAT con rc_host_rom.
# 2. tilesbin.py --a (TILESA.BIN), rombank.py (ROM.BIN, ROMRES.BIN, romres.txt), paquetes.py (MENUS.PAK, DEMO0-3.PAK),
#    tilesbin.py --b (TILES.BIN) y verpak.py sobre todo. Los ROMSTAT van todos (attract, pt y work/tramos, de
#    tools/corridas_tramos.sh) para que ROMRES.BIN y los bancos salgan iguales a los de romstat.py -o.
# 3. Carrera (H4-H5) si hay work/tramos/*/blk.bin: paquetes.py --tramos (GAMEN.PAK y ST00-11.PAK; GAMEL/GAMER solo si
#    hay corridas con ese coche) con las partidas de work/tramos y la de work/pt.
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
# menus: elegir coche recorriendo los tres (el cursor da la vuelta) y quedandose con cada uno, y elegir rally pasando por
# los cuatro, hasta el mapa del primer tramo (las demas corridas solo ven el coche y el rally por defecto)
MENU_C="1200-1208:C,1300-1308:S,1380-1386:R,1460-1466:R,1540-1546:R,1620-1626:L,1700-1706:L,1780-1786:L"
MENU_R="1880-1886:R,1950-1956:R,2020-2026:R,2090-2096:R,2160-2166:L"
if [ -z "$SIN_CORRIDAS" ]; then
  INPUTS= corrida attract 80000 125
  INPUTS=$(cat "$T/inputs_pt.txt") corrida pt 36000 60
fi
# la partida de Portugal con los otros dos coches (GAMEL.PAK y GAMER.PAK): el cursor de elegir coche a la izquierda (L,
# FE8124 = 04B0) o a la derecha (R, 0320) antes de elegir
if [ -z "$SIN_CORRIDAS" ] || [ ! -s "$W/ptR/blk.bin" ]; then
  INPUTS="1380-1386:L,$(cat "$T/inputs_pt.txt")" corrida ptL 12000 60
  INPUTS="1380-1386:R,$(cat "$T/inputs_pt.txt")" corrida ptR 12000 60
fi
if [ -z "$SIN_CORRIDAS" ] || [ ! -s "$W/menus2/blk.bin" ]; then
  INPUTS="$MENU_C,1840-1846:X,$MENU_R,2240-2246:X" corrida menus0 2800 30
  INPUTS="$MENU_C,1800-1806:L,1840-1846:X,$MENU_R,2240-2246:X" corrida menus1 2800 30
  INPUTS="$MENU_C,1800-1806:R,1840-1846:X,$MENU_R,2240-2246:X" corrida menus2 2800 30
fi
T1=$(date +%s); echo "corridas: $((T1 - T0)) s"
MEN="$W/menus0 $W/menus1 $W/menus2"
# las pantallas de menu de los otros rallies (elegir rally, resultados, continuar, fin): una partida de cada rally
MEN="$MEN $(for r in 0 1 2 3; do [ -s $W/tramos/${r}_10_150/blk.bin ] && echo $W/tramos/${r}_10_150; done)"
BLK="$W/attract/blk.bin $W/pt/blk.bin $W/ptL/blk.bin $W/ptR/blk.bin $(for d in $MEN; do echo $d/blk.bin; done)"
DUMPS="$W/attract/dumps.bin $W/pt/dumps.bin $W/ptL/dumps.bin $W/ptR/dumps.bin $(for d in $MEN; do echo $d/dumps.bin; done)"
RS="$W/attract/romstat.txt $W/pt/romstat.txt $W/ptL/romstat.txt $W/ptR/romstat.txt $(for d in $MEN; do echo $d/romstat.txt; done) $(ls $W/tramos/*/romstat.txt 2>/dev/null)"
# las corridas de los coches L y R por todos los rallies (tools/corridas_coches.sh): solo para GAMEL/GAMER.PAK
TR="$(ls -d $W/tramos/*/ $W/coches/*/ 2>/dev/null | while read d; do [ -s $d/blk.bin ] && echo $d; done) $W/pt $W/ptL $W/ptR"
python3 "$T/tilesbin.py" --a "$CD/TILESA.BIN"
cp "$W/romres.txt" "$W/romres_antes.txt" 2>/dev/null || true
python3 "$T/rombank.py" $RS -o "$CD" --romres "$W/romres.txt"
# el residente cambio (partidas nuevas): el codigo recompilado lee a direccion fija de rc_romres y tiene que ir con el
# mismo romres.txt que los bancos de los paquetes (si no, esos bloques no estan en ningun sitio: trampas en carrera)
if ! cmp -s "$W/romres.txt" "$W/romres_antes.txt"; then
  echo "romres.txt cambio: rc/rc68.py otra vez (y recompilar rc/rc_host con banco.sh para el banco)"
  python3 rc/rc68.py rc/wr2_rc.c
fi
python3 "$T/paquetes.py" --blk $BLK --dumps $DUMPS --romstat $RS -o "$CD"
python3 "$T/tilesbin.py" --b "$CD/TILES.BIN" --blk $BLK --dumps $DUMPS
[ -n "$(ls $W/tramos/*/blk.bin 2>/dev/null)" ] && python3 "$T/paquetes.py" --tramos $TR -o "$CD"
T2=$(date +%s); echo "construccion: $((T2 - T1)) s"
python3 "$T/verpak.py" "$CD/MENUS.PAK" "$CD/DEMO0.PAK" "$CD/DEMO1.PAK" "$CD/DEMO2.PAK" "$CD/DEMO3.PAK" "$CD/ROMRES.BIN" "$CD/TILES.BIN" "$CD/TILESA.BIN" \
  $(ls "$CD"/GAME?.PAK "$CD"/ST??.PAK 2>/dev/null)
ls -l "$CD"
echo "total: $(($(date +%s) - T0)) s"
