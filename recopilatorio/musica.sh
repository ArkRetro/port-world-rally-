#!/bin/sh
# Musica del titulo del WR2 para el menu: el sonido del WR2 sale del modelo del GAE1 (no hay pistas que copiar), asi
# que se graba lo que suena en el port: su imagen USA en mednafen (sin mando, el attract) hasta pasado el titulo, y
# musica.py corta la pieza. Hacen falta mednafen, Xvfb, ffmpeg y la BIOS (LICENCIA). Sale de las ROM: va a work/.
# Se puede poner otra en work/musica2.wav (mono o estereo, cualquier frecuencia: se usa entera).
set -e
cd "$(dirname "$0")"
R=$(pwd); mkdir -p work/mus
B=${LICENCIA:?define LICENCIA con tu BIOS (scph101.bin o similar)}
M=${MEDNAFEN:-$(command -v mednafen || echo /usr/games/mednafen)}
SEG=${SEG:-52}
rm -f work/mus/w2.mov
Xvfb :58 -screen 0 1024x768x24 >/dev/null 2>&1 & X=$!
sleep 1
DISPLAY=:58 SDL_AUDIODRIVER=dummy MEDNAFEN_HOME="$R/work/mus" "$M" -sound 1 -sound.driver sdl -psx.bios_na "$B" \
  -psx.bios_sanity 0 -video.driver sdl -qtrecord "$R/work/mus/w2.mov" -qtrecord.vcodec cscd \
  ../wrally2_ps1/ps1/wr2ps1_USA.cue > work/mus/mednafen.txt 2>&1 & P=$!
sleep "$SEG"
kill -TERM $P; wait $P || true; kill $X; wait $X 2>/dev/null || true
ffmpeg -v error -y -i work/mus/w2.mov -vn -ac 1 -ar 48000 work/mus/w2.wav
rm -f work/mus/w2.mov
echo "grabado: work/mus/w2.wav (attract del WR2, $SEG s)"
