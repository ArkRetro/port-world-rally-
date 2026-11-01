#!/bin/bash
# Herramienta: de tus ROM de MAME de World Rally 1 y 2 a las imagenes de CD de PS1 (bin/cue) de los dos ports y del
# disco con los dos y el menu (uso personal) - arkretro. Es lo que ejecuta el contenedor de herramienta/Dockerfile
# (crear.sh / crear.bat lo preparan y lo lanzan); tambien vale en un Linux con lo que instala ese Dockerfile.
# Uso: crear_imagen.sh [--bios fichero] [--salida carpeta] [--trabajo carpeta] [--regiones 'USA EUR'] fuente [...]
#   fuente   zips de MAME (wrally, wrallyd si el romset es split, wrally2 o wrally2a) o carpetas con ellos; las ROM se
#            reconocen por CRC (roms.py). Por defecto /roms (la carpeta que monta crear.sh).
#   --bios   tu BIOS de PS1 (scph1001.bin, scph101.bin...): el logo de PlayStation al arrancar (la BIOS solo lo dibuja
#            si el disco lleva el suyo; sin el no arranca en consolas PAL nuevas ni japonesas ni en PSIO/XStation) y la
#            musica del WR2 en el menu (se graba del port con mednafen, que necesita BIOS). Si no se da, se busca una
#            en las fuentes.
#   --salida donde quedan las imagenes (por defecto /salida)
#   --trabajo donde se construye (por defecto /trabajo/port): si se corta, al volver a lanzarlo sigue donde iba.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
BIOS=""; SALIDA=/salida; TRABAJO=/trabajo/port; REGIONES=${REGIONES:-"USA EUR"}; FUENTES=()
while [ $# -gt 0 ]; do
  case "$1" in
    --bios) BIOS=$2; shift 2;;
    --salida) SALIDA=$2; shift 2;;
    --trabajo) TRABAJO=$2; shift 2;;
    --regiones) REGIONES=$2; shift 2;;
    -h|--help) sed -n 2,16p "$0"; exit 0;;
    *) FUENTES+=("$1"); shift;;
  esac
done
[ ${#FUENTES[@]} -gt 0 ] || FUENTES=(/roms)
: "${NUGGET:?falta NUGGET (ruta de nugget)}"
T0=$(date +%s)
paso() { echo; echo "######## $* ($(( ($(date +%s) - T0) / 60 )) min)"; }

paso "1/6 copia del port en $TRABAJO"
mkdir -p "$TRABAJO" "$SALIDA"
if [ "$AQUI" != "$TRABAJO" ]; then cp -ru "$AQUI/." "$TRABAJO/"; fi
cd "$TRABAJO"

paso "2/6 ROM (por CRC)"
python3 herramienta/roms.py rom/wr1 rom/wr2 "${FUENTES[@]}"
if [ -z "$BIOS" ]; then
  BIOS=$(python3 - "${FUENTES[@]}" <<'P'
import os, sys
for f in sys.argv[1:]:
    for dp, _, fs in (os.walk(f) if os.path.isdir(f) else [('', [], [f])]):
        for x in sorted(fs):
            p = os.path.join(dp, x)
            if os.path.isfile(p) and os.path.getsize(p) == 0x80000 and b'Sony Computer Entertainment' in open(p, 'rb').read():
                print(p); sys.exit()
P
)
  [ -n "$BIOS" ] && echo "BIOS encontrada: $BIOS"
fi
if [ -n "$BIOS" ]; then export LICENCIA="$BIOS"
else echo "AVISO: sin BIOS. Las imagenes no llevan el logo de PlayStation (no arrancan en PAL nuevas, japonesas, PSIO ni"
     echo "       XStation) y el menu va sin la musica del WR2. Pon tu BIOS en la carpeta de las ROM o usa --bios."; fi

paso "3/6 World Rally 1 (unos minutos)"
WR_ROM="$TRABAJO/rom/wr1" SD_NOMBRE="World Rally PS1" wrally_ps1/construir.sh

paso "4/6 World Rally 2 (lo mas largo: corridas del banco de pruebas por los cuatro rallies)"
WR2_ROM="$TRABAJO/rom/wr2" PROC=${PROC:-$(nproc)} wrally2_ps1/construir.sh

paso "5/6 disco con los dos y el menu"
gcc -O2 -w -o wrally_ps1/perf/psxprof wrally_ps1/perf/psxprof.c -lm
gcc -O2 -w -o wrally2_ps1/perf/psxprof2 wrally2_ps1/perf/psxprof2.c -lm
REGIONES="$REGIONES" recopilatorio/construir.sh

paso "6/6 copia a $SALIDA"
for d in recopilatorio/sd wrally_ps1/ps1/sd wrally2_ps1/ps1/sd; do
  [ -d "$d" ] || continue
  for x in "$d"/*/; do rm -rf "${SALIDA:?}/$(basename "$x")"; cp -rL "$x" "$SALIDA/"; done
done
ls -1 "$SALIDA"
echo
echo "Hecho en $(( ($(date +%s) - T0) / 60 )) min. Imagenes en $SALIDA (uso personal: no las distribuyas)."
