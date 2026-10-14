#!/bin/sh
# EXE de la PS1 del World Rally 2 (R1: la ROM del 68000 en ROM.BIN, residente + bancos de DEMOr/STkk + trampa; menus
# con tiles A y demo con tiles B del CD, carrera en mosaico, sonido en la SPU) y la imagen de CD con los ficheros de
# work/cd/ (o MENUS=otro fichero; los que no esten, fuera). Tambien enlaza el EXE R0 (la ROM dentro) para la puerta G-RAM.
# Requiere banco.sh hecho (work/prog.bin, work/mcu.bin, rc/wr2_rc.c, rc/mcu_rc.c), mipsel-linux-gnu-gcc, NUGGET (ruta
# de pcsx-redux/src/mips/nugget, por sus .ld) y MKPSXISO. Lo generico se usa del port del WR1 por ruta (crt0.s, cd.c;
# CDOPT=-DCD_DMA para la copia del FIFO por DMA).
set -e
cd "$(dirname "$0")"
WR1=../../wrally_ps1
P=mipsel-linux-gnu
N=${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}
# de la MCU solo las tablas que lee MOVC, seguidas (mcu.c, MCU_TABLAS)
python3 -c "d = open('../work/mcu.bin', 'rb').read(); open('mcu_tab.bin', 'wb').write(d[0x1025:0x40E4] + d[0x5488:0x54CE])"
# la ROM del 68000 hasta 0xDD7FF (lo de detras no lo lee nadie) con las palabras en el orden de la PS1
python3 -c "import sys; d = open('../work/prog.bin', 'rb').read()[:0xDD800]; o = bytearray(len(d)); o[0::2] = d[1::2]; o[1::2] = d[0::2]; open('rom_sw.bin', 'wb').write(o)"
mkdir -p cd && cp SYSTEM.CNF cd/
# ficheros del CD (enlaces: no se copian 5 MB), en orden de uso
MAS=""
for f in ROM.BIN SPUFIJO.BIN SPUTIT.BIN MENUS.PAK DEMO0.PAK SPUDEMO0.BIN DEMO1.PAK SPUDEMO1.BIN DEMO2.PAK SPUDEMO2.BIN DEMO3.PAK SPUDEMO3.BIN \
         SPUMENU.BIN SPUS7E.BIN SPUCAR0.BIN SPUCAR1.BIN SPUCAR2.BIN ST00.PAK ST01.PAK ST02.PAK ST03.PAK ST04.PAK ST05.PAK \
         ST06.PAK ST07.PAK ST08.PAK ST09.PAK ST10.PAK ST11.PAK TILES.BIN TILESA.BIN; do
  src=../work/cd/$f; [ "$f" = MENUS.PAK ] && [ -n "$MENUS" ] && src=$MENUS
  rm -f cd/$f
  if [ -f "$src" ]; then ln -f "$src" cd/$f 2>/dev/null || cp "$src" cd/$f; MAS="$MAS $f"; else echo "sin $src: la imagen va sin $f"; fi
done
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -O2 -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fno-strict-aliasing -fdata-sections -ffunction-sections -w -I$N"
[ wr2_rc.o -nt ../rc/wr2_rc.c ] && [ wr2_rc.o -nt ../rc/rc_rt.h ] && [ wr2_rc.o -nt build.sh ] || $P-gcc $CF ${RCOPT:--Os} -c ../rc/wr2_rc.c -o wr2_rc.o
# lo que cambia entre R0 (la ROM dentro del EXE) y R1 (ROM.BIN al arrancar, residente + bancos del area + trampa)
for v in r0 r1; do
  D=""; [ $v = r1 ] && D="-DR1"
  $P-gcc $CF $D $MAINOPT -c main.c -o main_$v.o
  $P-gcc $CF $D $CARGAOPT -c carga.c -o carga_$v.o
  $P-gcc $CF $D -DRC_ROM_EXTERN -c ../rc/rc_mem.c -o rc_mem_$v.o
done
$P-gcc $CF $MAINOPT -c video.c -o video.o
$P-gcc $CF $CDOPT -c $WR1/ps1/cd.c -o cd.o
$P-gcc $CF -c ../rc/gae1.c -o gae1.o
$P-gcc $CF -c snd.c -o snd.o
$P-gcc $CF -DMCS51_XOR=1 -DMCU_TABLAS -c ../rc/mcu.c -o mcu.o
$P-gcc $CF -c reservas.c -o reservas.o
$P-gcc $CF -DMCS51_XOR=1 -c ../rc/mcu_rc.c -o mcu_rc.o
$P-gcc $ARCH -c data.s -o data.o
$P-gcc $ARCH -c data_r1.s -o data_r1.o
$P-gcc $ARCH -c $WR1/ps1/crt0.s -o crt0.o
LD="$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,--oformat=elf32-tradlittlemips"
O="crt0.o video.o cd.o snd.o wr2_rc.o gae1.o mcu.o mcu_rc.o"
# puerta G-RAM: R0 (la ROM en el EXE; no va al CD) y el EXE de tamanos R1 (con las reservas del codigo que falta),
# que solo se enlaza para medir el area de tramo
$LD -Wl,-Map=w2ps1_r0.map -o w2ps1_r0.elf $O main_r0.o carga_r0.o rc_mem_r0.o reservas.o data.o -lgcc
python3 ../tools/gram.py w2ps1_r0.map
$P-gcc $CF -DR1 -DRES_CODIGO=$(python3 ../tools/gram.py --codigo w2ps1_r0.map) -c reservas.c -o reservas_r1.o
$LD -Wl,-Map=w2ps1_r1.map -o w2ps1_r1.elf $O main_r1.o carga_r1.o rc_mem_r1.o reservas_r1.o data_r1.o -lgcc
python3 ../tools/gram.py --r1 w2ps1_r1.map
# el EXE del CD: R1
$LD -Wl,-Map=w2ps1.map -o w2ps1.elf $O main_r1.o carga_r1.o rc_mem_r1.o reservas.o data_r1.o -lgcc
$P-objcopy -O binary w2ps1.elf w2ps1.ps-exe
$P-size w2ps1.elf
nm -S --size-sort w2ps1.elf | tail -8
ls -l w2ps1.ps-exe
cp w2ps1.ps-exe cd/WR2PS1.EXE
$P-nm w2ps1.elf > w2ps1.nm
MKISO_MAS="$MAS" python3 mkiso.py
