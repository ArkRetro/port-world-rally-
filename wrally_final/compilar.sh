#!/bin/sh
# Compila "WRally neogeo final" sin tocar el codigo fuente: copia el arbol a un directorio
# de trabajo, cambia las rutas fijas (/home/claude/wrally, /tmp/claude-0) por las de ese
# directorio y sigue los pasos de COMPILAR.txt.
#   uso: ./compilar.sh <carpeta con las ROM del set wrally> [directorio de trabajo]
# Resultado: <trabajo>/neo/build/wrally.neo (NeoSD / MiSTer) y <trabajo>/neo/mame/ (MAME).
set -e
ROM=$(cd "${1:?falta la carpeta de las ROM}" && pwd)
WORK=${2:-$(pwd)/build_final}
SRC=$(cd "$(dirname "$0")" && pwd)

rm -rf "$WORK"
mkdir -p "$WORK/rom" "$WORK/tmp" "$WORK/neo/build"
cp -r "$SRC/cov" "$SRC/neo" "$SRC/mkprog.py" "$WORK/"
for f in worldr14.c01 worldr15.c03 worldr16.c22 worldr17.c23 worldr18.i07 worldr19.i09 \
         worldr20.i11 worldr21.i13 wrdallas.bin; do
    p=$(find "$ROM" -name "$f" | head -1)
    [ -n "$p" ] || { echo "falta la ROM $f en $ROM"; exit 1; }
    cp "$p" "$WORK/rom/"
done
grep -rlI -e /home/claude/wrally -e /tmp/claude-0 "$WORK" | \
    xargs sed -i -e "s#/tmp/claude-0/#$WORK/tmp/#g" -e "s#/home/claude/wrally#$WORK#g"

cd "$WORK"
python3 mkprog.py "$WORK/rom/"
gcc -O2 -o neo/snd/adpcma_enc neo/snd/adpcma_enc.c
cd neo
python3 gfxconv.py
python3 build.py
python3 pack.py
echo "listo: $WORK/neo/build/wrally.neo"
