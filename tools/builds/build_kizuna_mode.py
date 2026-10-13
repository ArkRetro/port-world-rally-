#!/usr/bin/env python3
"""WR v6 'Kizuna mode': same cartridge structure and identity as Kizuna Encounter (works on AES + NeoSD).
usage: build_kizuna_mode.py <comply.neo from build_comply.py> <kizuna.neo> <out.neo>
 * input already has: IRQ/line-A vectors via P1 trampolines, 6-byte header jump slots, no write to 0x2FFFF0
 * .neo header: NGH, year and genre copied from Kizuna (NeoSD per-game profile), WR sizes/name kept
 * cartridge header NGH (CPU 0x108) set to the same NGH (P region is word-swapped in the .neo)"""
import struct, sys
co = bytearray(open(sys.argv[1], 'rb').read()); kz = open(sys.argv[2], 'rb').read(4096)
for off in (0x1C, 0x20, 0x28):
    co[off:off+4] = kz[off:off+4]
ngh = struct.unpack('<I', kz[0x28:0x2C])[0]
o = 0x1000 + 0x108
co[o] = ngh & 0xFF; co[o+1] = (ngh >> 8) & 0xFF
open(sys.argv[3], 'wb').write(co)
print('NGH 0x%X written to .neo header and cartridge header' % ngh)
