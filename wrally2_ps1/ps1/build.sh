#!/bin/sh
# EXE de la PS1 del World Rally 2 (hito H2: ROM dentro del EXE, video en mosaico, sin sonido) y la imagen de CD.
# Requiere banco.sh hecho (work/prog.bin, work/mcu.bin, rc/wr2_rc.c, rc/mcu_rc.c), mipsel-linux-gnu-gcc, NUGGET (ruta
# de pcsx-redux/src/mips/nugget, por sus .ld) y MKPSXISO. Lo generico se usa del port del WR1 por ruta (crt0.s).
set -e
cd "$(dirname "$0")"
WR1=../../wrally_ps1
P=mipsel-linux-gnu
N=${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}
cp ../work/mcu.bin mcu.bin
# la ROM del 68000 hasta 0xDD7FF (lo de detras no lo lee nadie) con las palabras en el orden de la PS1
python3 -c "import sys; d = open('../work/prog.bin', 'rb').read()[:0xDD800]; o = bytearray(len(d)); o[0::2] = d[1::2]; o[1::2] = d[0::2]; open('rom_sw.bin', 'wb').write(o)"
mkdir -p cd && cp SYSTEM.CNF cd/
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -O2 -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fno-strict-aliasing -fdata-sections -ffunction-sections -w -I$N"
[ wr2_rc.o -nt ../rc/wr2_rc.c ] && [ wr2_rc.o -nt ../rc/rc_rt.h ] && [ wr2_rc.o -nt build.sh ] || $P-gcc $CF ${RCOPT:--Os} -c ../rc/wr2_rc.c -o wr2_rc.o
$P-gcc $CF $MAINOPT -c main.c -o main.o
$P-gcc $CF -DRC_ROM_EXTERN -c ../rc/rc_mem.c -o rc_mem.o
$P-gcc $CF -c ../rc/gae1.c -o gae1.o
$P-gcc $CF -DMCS51_XOR=1 -c ../rc/mcu.c -o mcu.o
$P-gcc $CF -DMCS51_XOR=1 -c ../rc/mcu_rc.c -o mcu_rc.o
$P-gcc $ARCH -c data.s -o data.o
$P-gcc $ARCH -c $WR1/ps1/crt0.s -o crt0.o
$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,-Map=w2ps1.map -Wl,--oformat=elf32-tradlittlemips \
    -o w2ps1.elf crt0.o main.o wr2_rc.o rc_mem.o gae1.o mcu.o mcu_rc.o data.o -lgcc
$P-objcopy -O binary w2ps1.elf w2ps1.ps-exe
$P-size w2ps1.elf
nm -S --size-sort w2ps1.elf | tail -8
ls -l w2ps1.ps-exe
cp w2ps1.ps-exe cd/WR2PS1.EXE
python3 mkiso.py
