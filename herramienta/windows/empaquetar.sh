#!/bin/sh
# Paquete del creador de imagenes para Windows (herramienta/windows): dist/WorldRallyPS1-Creador.zip con
#   'Crear imagenes PS1.exe' (creador.c), LEEME.txt, app/creador.py, app/python (Python embebido de python.org, sin
#   el ._pth para que los scripts vean su carpeta) y app/port (el codigo del port: lo que esta en git).
# Nada de las ROM ni de la BIOS: el paquete se puede compartir. Hace falta x86_64-w64-mingw32-gcc y windres, python3
# con pillow (el icono) y curl. Uso: herramienta/windows/empaquetar.sh [destino (dist)]
set -e
cd "$(dirname "$0")"
R=$(cd ../.. && pwd); D=$(mkdir -p "${1:-$R/dist}" && cd "${1:-$R/dist}" && pwd); P=$D/WorldRallyPS1-Creador
PYZIP=python-3.12.7-embed-amd64.zip; PYSUMA=0d57bb6cb078b74d23dbfe91f77d6780d45bed328911609f1f7ee2ba1606bf44
rm -rf "$P"; mkdir -p "$P/app/python" "$P/app/port"
[ -f "$D/$PYZIP" ] || curl -sSL -o "$D/$PYZIP" https://www.python.org/ftp/python/3.12.7/$PYZIP
echo "$PYSUMA  $D/$PYZIP" | sha256sum -c -
python3 -c "import zipfile, sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$D/$PYZIP" "$P/app/python"
rm -f "$P"/app/python/*._pth
python3 -c "
from PIL import Image
im = Image.open('$R/wrally_ps1/ps1/logo_pina.png').convert('RGBA')
im.save('creador.ico', sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])"
x86_64-w64-mingw32-windres creador.rc -O coff -o creador.res
x86_64-w64-mingw32-gcc -municode -mwindows -O2 -s -Wall -o "$P/Crear imagenes PS1.exe" creador.c creador.res \
  -lcomctl32 -lole32 -lshell32 -luuid
rm -f creador.res creador.ico
cp creador.py "$P/app/"
(cd "$R" && git ls-files wrally_ps1 wrally2_ps1 recopilatorio herramienta | grep -v '^herramienta/windows/' |
  tar -cf - -T -) | tar -xf - -C "$P/app/port"
sed 's/$/\r/' LEEME.txt > "$P/LEEME.txt"
(cd "$D" && rm -f WorldRallyPS1-Creador.zip && zip -q -r -9 WorldRallyPS1-Creador.zip WorldRallyPS1-Creador)
ls -l "$D/WorldRallyPS1-Creador.zip"
