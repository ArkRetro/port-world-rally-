#!/bin/sh
# Disco con los dos World Rally y un menu para elegir (uso personal) - arkretro. Hacen falta los dos ports construidos
# (wrally_ps1/construir.sh y los de wrally2_ps1: sus ps1/cd/ y su iso_USA.xml) y:
#   NUGGET   ruta de nugget (pcsx-redux/src/mips), para el enlazado (ps-exe.ld)
#   MKPSXISO ejecutable de mkpsxiso (por defecto el del PATH)
#   LICENCIA tu BIOS (o LICENSEA.DAT): logo de PlayStation del arranque (wrally_ps1/ps1/licencia.py)
#   SD_NOMBRE, REGIONES, XSTATION, PSIO: ver mkiso.py
# Las portadas del menu (work/portada1.ppm y portada2.ppm) las saca captura.sh del titulo de cada port si no estan
# (se pueden poner otras: capturas de 368 x 240 en ppm o png); SIN_PORTADAS=1: el menu con un 1 y un 2 en su lugar.
# La musica del menu (musica.py): la del WR1 de sus pistas de CD-DA y la del titulo del WR2 grabada del port con
# mednafen (musica.sh, si no esta work/mus/w2.wav ni work/musica2.wav); SIN_MUSICA=1: el menu sin musica.
set -e
cd "$(dirname "$0")"
P=mipsel-linux-gnu
N=${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}
mkdir -p work cd
if [ -z "$SIN_PORTADAS" ] && { [ ! -f work/portada1.ppm ] || [ ! -f work/portada2.ppm ]; }; then ./captura.sh; fi
echo "== pantalla del menu"
python3 pantalla.py work/pantalla.bin work/portada1.ppm work/portada2.ppm work/vista.png
echo "== musica del menu"
if [ -n "$SIN_MUSICA" ]; then python3 -c "open('work/musica.bin','wb').write(bytes(24))"
else
  if [ ! -f work/musica2.wav ] && [ ! -f work/mus/w2.wav ]; then
    if [ -n "$LICENCIA" ]; then ./musica.sh; else echo "sin LICENCIA (BIOS) no se graba la musica del WR2"; fi
  fi
  python3 musica.py work/musica.bin
fi
echo "== INICIO.EXE"
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -Os -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fdata-sections -ffunction-sections -Wall"
$P-gcc $CF -c menu.c -o work/menu.o
$P-gcc $ARCH -c inicio.s -o work/inicio.o
$P-gcc $ARCH -Iwork -c datos.s -o work/datos.o
$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,--oformat=elf32-tradlittlemips \
  -Wl,-Map=work/menu.map -o work/menu.elf work/inicio.o work/menu.o work/datos.o -lgcc
$P-objcopy -O binary work/menu.elf cd/INICIO.EXE
$P-size work/menu.elf
echo "== imagenes"
python3 mkiso.py
ls -l wr12ps1_*.bin wr12ps1_*.cue
