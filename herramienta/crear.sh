#!/bin/sh
# Lanzador para Linux y macOS (y Windows con WSL): construye el contenedor de la herramienta (la primera vez, unos
# minutos) y lo ejecuta con tus ROM. Hace falta Docker (o Podman: DOCKER=podman).
# Uso: herramienta/crear.sh carpeta_con_las_roms [carpeta_de_salida (./salida)]
#   En la carpeta de las ROM: los zip de MAME de wrally (y wrallyd si tu romset es split) y wrally2 (o wrally2a), y si
#   quieres el logo de PlayStation y la musica del WR2 en el menu, tu BIOS (scph1001.bin, scph101.bin...).
set -e
D=${DOCKER:-docker}
R=$(cd "$(dirname "$0")/.." && pwd)
ROMS=$(cd "${1:?uso: crear.sh carpeta_con_las_roms [salida]}" && pwd)
mkdir -p "${2:-salida}"
OUT=$(cd "${2:-salida}" && pwd)
"$D" build -t wr-ps1 -f "$R/herramienta/Dockerfile" "$R"
"$D" run --rm -v "$ROMS:/roms:ro" -v "$OUT:/salida" -v wr-ps1-trabajo:/trabajo wr-ps1
echo "Imagenes en $OUT"
