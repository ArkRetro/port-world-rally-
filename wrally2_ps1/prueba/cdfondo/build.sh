#!/bin/sh
# Sonda cdfondo: EXE (CDFONDO.EXE) e imagenes USA/EUR con sd/ para PSIO y XStation. Requiere mipsel-linux-gnu-gcc,
# NUGGET (ruta de pcsx-redux/src/mips, por sus .ld) y MKPSXISO; LICENCIA* para los sectores de licencia (licencia.py
# del WR1). crt0.s y cd.c/cd.h se toman del port del WR1 por ruta, sin cambios: cd.c se compila dos veces (copia por
# CPU y -DCD_DMA) y a la de DMA se le ponen los simbolos d_* con objcopy (no tiene simbolos externos: se comprueba).
# Variables: DURACION (s por variante, 60), SECTORES (de DATOS.BIN, 6000), SD_NOMBRE.
set -e
cd "$(dirname "$0")"
WR1=../../../wrally_ps1
P=mipsel-linux-gnu
N=${NUGGET:?define NUGGET con la ruta de pcsx-redux/src/mips (nugget)}
python3 gen.py
mkdir -p cd && cp SYSTEM.CNF cd/
ARCH="-march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc -fno-stack-protector -nostdlib -ffreestanding"
CF="$ARCH -O2 -G0 -mno-gpopt -fomit-frame-pointer -fno-builtin -fno-strict-aliasing -fdata-sections -ffunction-sections -w -I$N"
$P-gcc $CF -DDURACION=${DURACION:-60} -c main.c -o main.o
$P-gcc $CF -c $WR1/ps1/cd.c -o cd_cpu.o
$P-gcc $CF -DCD_DMA -c $WR1/ps1/cd.c -o cd_dma0.o
[ -z "$($P-nm -u cd_dma0.o)" ] || { echo "cd.c con simbolos externos: el prefijo d_ los romperia" >&2; $P-nm -u cd_dma0.o >&2; exit 1; }
$P-objcopy --prefix-symbols=d_ cd_dma0.o cd_dma.o
$P-gcc $ARCH -c $WR1/ps1/crt0.s -o crt0.o
$P-gcc $ARCH -nostdlib -static -Wl,--gc-sections -T$N/nooverlay.ld -T$N/ps-exe.ld -Wl,--oformat=elf32-tradlittlemips \
    -Wl,-Map=cdfondo.map -o cdfondo.elf crt0.o main.o cd_cpu.o cd_dma.o -lgcc
$P-objcopy -O binary cdfondo.elf cdfondo.ps-exe
$P-nm cdfondo.elf > cdfondo.nm
$P-size cdfondo.elf
cp cdfondo.ps-exe cd/CDFONDO.EXE
MKISO_EXE=CDFONDO.EXE MKISO_IMG=cdfondo MKISO_MAS=DATOS.BIN SD_NOMBRE="${SD_NOMBRE:-WR2 CDFONDO}" python3 ../../ps1/mkiso.py
