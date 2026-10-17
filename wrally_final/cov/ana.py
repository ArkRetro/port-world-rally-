"""Analisis estatico del programa de World Rally: descenso recursivo + semillas de traza."""
import capstone, struct, sys, pickle
from capstone import m68k as M
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
md.detail = True
D = open('/home/claude/wrally/prog.bin', 'rb').read()
SIZE = len(D)

def dis(a):
    try:
        return next(md.disasm(D[a:a + 10], a))
    except StopIteration:
        return None

seeds = set()
v = struct.unpack('>64I', D[:256])
for x in v[1:]:
    if x < SIZE: seeds.add(x)
for fn in ['pcs_play.txt']:
    for l in open(fn):
        x = int(l, 16)
        if x < SIZE: seeds.add(x)
for l in open('/home/claude/wrally/mame/trace68k.txt'):
    p = l.split(':')[0].strip()
    try:
        x = int(p, 16)
        if x < SIZE: seeds.add(x)
    except ValueError: pass

import gcrypt
EXTRA = [0x9eb6, 0xa32a, 0x4d4e, 0x6476, 0x3224]
EXTRA += [struct.unpack('>I', D[0x143ee+4*i:0x143f2+4*i])[0] for i in range(16)]
seeds.update(EXTRA)
code = {}        # addr -> insn
indirect = []    # (addr, text)
bad = []
work = list(seeds)
END = {'rts', 'rte', 'rtr', 'jmp', 'bra', 'illegal', 'stop'}
while work:
    a = work.pop()
    while a not in code:
        if a >= SIZE or a & 1: bad.append(a); break
        i = dis(a)
        if i is None: bad.append(a); break
        code[a] = i
        mn = i.mnemonic.split('.')[0]
        nxt = a + i.size
        tgt = None
        if mn in ('bra', 'bsr') or (mn.startswith('b') and mn[1:3] in ('cc','cs','eq','ne','ge','gt','hi','le','ls','lt','mi','pl','vc','vs')) or mn.startswith('db'):
            op = i.operands[-1]
            if op.type == M.M68K_OP_BR_DISP:
                tgt = a + 2 + op.br_disp.disp
        elif mn in ('jmp', 'jsr'):
            op = i.operands[0]
            am = op.address_mode
            if am in (M.M68K_AM_ABSOLUTE_DATA_LONG, M.M68K_AM_ABSOLUTE_DATA_SHORT):
                tgt = op.imm if hasattr(op,'imm') else None
                tgt = struct.unpack('>I', D[a+2:a+6])[0] if am == M.M68K_AM_ABSOLUTE_DATA_LONG else struct.unpack('>h', D[a+2:a+4])[0] & 0xffffff
            elif am == M.M68K_AM_PCI_DISP:
                tgt = a + 2 + struct.unpack('>h', D[a+2:a+4])[0]
            else:
                indirect.append((a, i.mnemonic + ' ' + i.op_str))
        if tgt is not None:
            tgt &= 0xffffff
            if tgt not in code: work.append(tgt)
        if mn in END: break
        a = nxt
print('instrucciones', len(code), 'bytes', sum(i.size for i in code.values()))
print('indirectos', len(indirect))
for a, t in sorted(indirect): print('  %06x %s' % (a, t))
print('malos', [hex(x) for x in bad[:20]], len(bad))
pickle.dump({a: (i.size, i.mnemonic, i.op_str) for a, i in code.items()}, open('code.pkl', 'wb'))
