#!/bin/sh
# Herramienta: de tus ROM de MAME de World Rally 1 y 2 a las imagenes de CD de PS1 (bin/cue) de los dos ports y del
# disco con los dos y el menu (uso personal) - arkretro. Es lo que ejecutan el programa de Windows (herramienta/windows)
# y el contenedor de herramienta/Dockerfile; tambien vale en un Linux con lo que instala ese Dockerfile. sh POSIX
# (busybox en Windows).
# Uso: crear_imagen.sh [--bios fichero] [--salida carpeta] [--trabajo carpeta] [--regiones 'USA EUR'] fuente [...]
#   fuente   zips de MAME (wrally, wrallyd si el romset es split, wrally2 o wrally2a) o carpetas con ellos; las ROM se
#            reconocen por CRC (roms.py). Por defecto /roms (la carpeta que monta crear.sh).
#   --bios   tu BIOS de PS1 (scph1001.bin, scph101.bin...): el logo de PlayStation al arrancar (la BIOS solo lo dibuja
#            si el disco lleva el suyo; sin el no arranca en consolas PAL nuevas ni japonesas ni en PSIO/XStation). Si
#            no se da, se busca una en las fuentes.
#   --salida donde quedan las imagenes (por defecto /salida)
#   --trabajo donde se construye (por defecto /trabajo/port): si se corta, al volver a lanzarlo sigue donde iba.
# Las lineas "######## n/6" son los pasos (el programa de Windows las usa para la barra de progreso).
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
BIOS=""; SALIDA=/salida; TRABAJO=/trabajo/port; REGIONES=${REGIONES:-"USA EUR"}
while [ $# -gt 0 ]; do
  case "$1" in
    --bios) BIOS=$2; shift 2;;
    --salida) SALIDA=$2; shift 2;;
    --trabajo) TRABAJO=$2; shift 2;;
    --regiones) REGIONES=$2; shift 2;;
    -h|--help) sed -n 2,15p "$0"; exit 0;;
    *) break;;
  esac
done
[ $# -gt 0 ] || set -- /roms
: "${NUGGET:?falta NUGGET (ruta de nugget)}"
T0=$(date +%s)
paso() { echo; echo "######## $* ($(( ($(date +%s) - T0) / 60 )) min)"; }

paso "1/6 copia del port en $TRABAJO"
mkdir -p "$TRABAJO" "$SALIDA"
if [ "$AQUI" != "$(cd "$TRABAJO" && pwd)" ]; then
  python3 -c "import shutil, sys; shutil.copytree(sys.argv[1], sys.argv[2], dirs_exist_ok=True,
    ignore=shutil.ignore_patterns('.git', 'work', 'sd', '__pycache__'))" "$AQUI" "$TRABAJO"
fi
cd "$TRABAJO"

paso "2/6 ROM (por CRC)"
python3 herramienta/roms.py rom/wr1 rom/wr2 "$@"
if [ -z "$BIOS" ]; then
  BIOS=$(python3 - "$@" <<'P'
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
else echo "AVISO: sin BIOS. Las imagenes no llevan el logo de PlayStation: arrancan en MiSTer y en consolas americanas,"
     echo "       pero no en PSIO, XStation, PS1 PAL modernas ni japonesas. Pon tu BIOS con las ROM para tenerlo."; fi

paso "3/6 World Rally 1"
WR_ROM="$TRABAJO/rom/wr1" sh wrally_ps1/construir.sh

paso "4/6 World Rally 2 (lo mas largo)"
WR2_ROM="$TRABAJO/rom/wr2" sh wrally2_ps1/construir.sh

paso "5/6 disco con los dos y el menu"
gcc -O2 -w -o wrally_ps1/perf/psxprof wrally_ps1/perf/psxprof.c -lm
gcc -O2 -w -o wrally2_ps1/perf/psxprof2 wrally2_ps1/perf/psxprof2.c -lm
REGIONES="$REGIONES" sh recopilatorio/construir.sh

paso "6/6 copia a $SALIDA"
python3 - "$SALIDA" recopilatorio/sd wrally_ps1/ps1/sd wrally2_ps1/ps1/sd <<'P'
import os, shutil, sys
sal = sys.argv[1]
for d in sys.argv[2:]:
    if not os.path.isdir(d): continue
    for x in sorted(os.listdir(d)):
        if not os.path.isdir(os.path.join(d, x)) or 'prueba' in x: continue
        dst = os.path.join(sal, x); shutil.rmtree(dst, ignore_errors=True); shutil.copytree(os.path.join(d, x), dst)
        print(x)
P
echo
echo "Hecho en $(( ($(date +%s) - T0) / 60 )) min. Imagenes en $SALIDA (uso personal: no las distribuyas)."
