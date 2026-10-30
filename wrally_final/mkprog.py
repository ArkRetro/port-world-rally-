"""Genera /home/claude/wrally/prog.bin (programa 68000 de World Rally, 1 MB) a partir de las ROM
del propio usuario (set MAME 'wrally'): byte par = worldr17.c23, byte impar = worldr16.c22."""
import sys
R = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/wrally/rom/'
hi = open(R + 'worldr17.c23', 'rb').read()
lo = open(R + 'worldr16.c22', 'rb').read()
open('/home/claude/wrally/prog.bin', 'wb').write(bytes(b for p in zip(hi, lo) for b in p))
print('prog.bin listo')
