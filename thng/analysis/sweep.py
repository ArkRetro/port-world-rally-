#!/usr/bin/env python3
"""Linear sweep of the gaps the recursive disassembly left in the code area of the Thunder Hoop program.

usage: sweep.py <th_cpu.bin> <code_insns.txt> <trap_tables.csv> <lo> <hi> <out_swept.txt>
A gap [a, b) between known instructions is decoded linearly from a; the run is kept when every
instruction decodes, none is a 0000/FFFF word (padding, data), and it ends with a flow end
(rts/rte/jmp/bra/trap #1, whose inline table of encrypted pointers is data) or runs
into known code.  Output: same format as code_insns.txt ("pc size mnemonic ops").
"""
import sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000, CsError

rom = open(sys.argv[1], 'rb').read()
known = {}
for line in open(sys.argv[2]):
    p = line.split(None, 2)
    known[int(p[0], 16)] = int(p[1])
kb = set()
for a, n in known.items():
    kb.update(range(a, a + n))
for line in open(sys.argv[3]).readlines()[1:]:      # trap #1 tables: data, never swept
    t0, n = line.strip().split(',')
    kb.update(range(int(t0, 16), int(t0, 16) + 4 * int(n)))
lo, hi = int(sys.argv[4], 16), int(sys.argv[5], 16)
cs = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
cs.detail = True
END = ('rts', 'rte', 'rtr', 'jmp', 'bra')
out = []
a = lo
while a < hi:
    if a in kb:
        a += 2
        continue
    start = a
    run = []
    ok = False
    while a < hi and a not in kb:
        try:
            i = next(cs.disasm(rom[a:a + 12], a, 1), None)
            if i is not None:
                i.operands
        except (CsError, StopIteration):
            i = None
        if i is None or i.mnemonic.startswith('dc') or rom[a:a + 2] in (b'\x00\x00', b'\xff\xff') or i.mnemonic in ('illegal', 'reset', 'stop'):
            break
        run.append(i)
        a += i.size
        if i.mnemonic == 'trap' and i.op_str in ('#$3', '#$5'):
            a += 2                      # inline parameter word
        if i.mnemonic.split('.')[0] in END or (i.mnemonic == 'trap' and i.op_str == '#$1'):
            ok = True
            break
    else:
        ok = bool(run) and a in kb
    if ok and len(run) >= 2:
        out += run
    else:
        a = start + 2
print('swept runs: %d instructions, %d bytes' % (len(out), sum(i.size for i in out)))
with open(sys.argv[6], 'w') as f:
    for i in out:
        f.write('%06X %d %s %s\n' % (i.address, i.size, i.mnemonic, i.op_str))
