#!/bin/sh
# Sonda spucad: EXE (SPUCAD.EXE) e imagenes USA/EUR con sd/ para PSIO y XStation. Requiere mipsel-linux-gnu-gcc,
# NUGGET (ruta de pcsx-redux/src/mips, por sus .ld) y MKPSXISO; LICENCIA* para los sectores de licencia (licencia.py
# del WR1). El crt0.s se toma del port del WR1 por ruta. Variables: DURACION (s, 240), ROMPE (0; 1 = encolar sin
# esperar a que la voz toque el anterior; 2 = sin silencio al entrar y encolar tarde), SD_NOMBRE.
set -e
cd "$(dirname "$0")"
WR1=../../../wrally_ps1
P=mipsel-linux-gnu
N=${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}
python3 gen.py
mkdir -p cd && cp SYSTEM.CNF cd/
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -O2 -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fno-strict-aliasing -fdata-sections -ffunction-sections -w -I$N"
$P-gcc $CF -DDURACION=${DURACION:-240} -DROMPE=${ROMPE:-0} -c main.c -o main.o
$P-gcc $ARCH -c data.s -o data.o
$P-gcc $ARCH -c $WR1/ps1/crt0.s -o crt0.o
$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,--oformat=elf32-tradlittlemips \
    -Wl,-Map=spucad.map -o spucad.elf crt0.o main.o data.o -lgcc
$P-objcopy -O binary spucad.elf spucad.ps-exe
$P-nm spucad.elf > spucad.nm
$P-size spucad.elf
cp spucad.ps-exe cd/SPUCAD.EXE
MKISO_EXE=SPUCAD.EXE MKISO_IMG=spucad SD_NOMBRE="${SD_NOMBRE:-WR2 SPUCAD}" python3 ../../ps1/mkiso.py
