"""Prepara los datos a partir de tus ROM de MAME de World Rally 2 (carpeta o zip en WR2_ROM, por defecto ./rom):
programa del 68000 (work/prog.bin), MCU DS5002FP (work/mcu.bin) y region gfx1 (work/gfx1.bin, graficos + sonido)"""
import sys, os, hashlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import work, ROM
from romset import RomSet
import gfx

rs = RomSet(sys.argv[1] if len(sys.argv) > 1 else ROM)
a = rs.read('wr2_64.ic64'); b = rs.read('wr2_63.ic63')       # ROM_LOAD16_BYTE: ic64 = bytes pares, ic63 = impares
assert len(a) == len(b) == 0x80000
p = bytearray(0x100000); p[0::2] = a; p[1::2] = b
open(work('prog.bin'), 'wb').write(p)
m = rs.read('wrally2_ds5002fp_sram.bin'); assert len(m) == 0x8000
import zlib
if zlib.crc32(m) != 0x4c532e9e: print('AVISO: la SRAM de la MCU no es la de MAME (CRC %08x, se espera 4c532e9e)' % zlib.crc32(m))
open(work('mcu.bin'), 'wb').write(m)
g, juego = gfx.construir(rs)
open(work('gfx1.bin'), 'wb').write(g)
md5 = lambda x: hashlib.md5(x).hexdigest()
print('prog.bin %s  mcu.bin %s  gfx1.bin (%s) %s  en %s' % (md5(p), md5(m), juego, md5(g), work('')))
