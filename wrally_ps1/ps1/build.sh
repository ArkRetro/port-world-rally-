#!/bin/sh
# compilacion de la fase 2 para PS1 (PS-EXE)
set -e
cd "$(dirname "$0")"
cp ../work/mcu.bin mcu.bin
mkdir -p cd && cp SYSTEM.CNF cd/
P=mipsel-linux-gnu
N=${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -O2 -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fno-strict-aliasing -fdata-sections -ffunction-sections -w -I$N"
[ wr_rc.o -nt ../rc/wr_rc.c ] && [ wr_rc.o -nt ../rc/rc_rt.h ] && [ wr_rc.o -nt build.sh ] || $P-gcc $CF ${RCOPT:--Os} -c ../rc/wr_rc.c -o wr_rc.o
$P-gcc $CF -c main.c -o main.o
$P-gcc $CF -c cd.c -o cd.o
$P-gcc $CF -c snd.c -o snd.o
$P-gcc $CF -c ../snd/snd_tab.c -o snd_tab.o
$P-gcc $CF -c ../snd/oki_tab.c -o oki_tab.o
$P-gcc $CF -DMCS51_XOR=1 -c ../rc/mcu_rc.c -o mcu_rc.o
$P-gcc $CF -DMCS51_XOR=1 -c ../rc/mcs51.c -o mcs51.o
$P-gcc $CF -c ../rc/gcrypt.c -o gcrypt.o
$P-gcc $CF -DRC_ROM_EXTERN -c ../rc/rc_mem.c -o rc_mem.o
$P-gcc $ARCH -c data.s -o data.o
$P-gcc $ARCH -I$N -c $N/common/crt0/crt0.s -o crt0.o
$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,-Map=wr2.map -Wl,--oformat=elf32-tradlittlemips -o wr2.elf crt0.o main.o wr_rc.o mcs51.o mcu_rc.o gcrypt.o rc_mem.o cd.o snd.o snd_tab.o oki_tab.o data.o -lgcc
$P-objcopy -O binary wr2.elf wr2.ps-exe
ls -l wr2.ps-exe
cp wr2.ps-exe cd/WRPS1.EXE
python3 mkiso.py && ls -l wrps1_*.bin
