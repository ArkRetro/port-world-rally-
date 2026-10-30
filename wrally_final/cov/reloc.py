"""Calcula los parches de reubicacion de memoria (World Rally -> Neo Geo)."""
import capstone, struct, pickle, sys
from capstone import m68k as M
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
md.detail = True
D = open('/home/claude/wrally/prog.bin', 'rb').read()
code = pickle.load(open('code.pkl', 'rb'))

# rangos origen -> destino Neo Geo
RANGES = [
    (0xFEC000, 0xFF0000, 0x10C000, 'work'),
    (0x200000, 0x204000, 0x104000, 'pal'),
    (0x440000, 0x441000, 0x109000, 'spr'),
    (0x700000, 0x700100, 0x10A000, 'io'),
]
KEEP = [(0x100000, 0x104000, 'vram'), (0x108000, 0x108010, 'vregs')]
def remap(v):
    a = v & 0xFFFFFF
    for lo, hi, dst, n in RANGES:
        if lo <= a < hi: return dst + (a - lo), n
    return None, None

# codigo especulativo en huecos pequenos
cov = set()
for a, (s, m, o) in code.items():
    cov.update(range(a, a + s))
SPEC = [(0x6806,0x6824),(0x6894,0x68a6),(0xb1b2,0xb20a),(0xb24a,0xb25e),(0xdc0a,0xdc18),(0x102bc,0x102f2),
        (0x11aac,0x11ae2),(0x11df4,0x11e20),(0x11e3e,0x11e4e),(0x1215c,0x12172),(0x12656,0x126d0)]
allc = {}
for a, (s, m, o) in code.items(): allc[a] = True
spec_ok = []
for lo, hi in SPEC:
    a = lo; ok = True; lst = []
    while a < hi:
        try: i = next(md.disasm(D[a:a+10], a))
        except StopIteration: ok = False; break
        if i.mnemonic in ('illegal',) or i.mnemonic.startswith('dc'): ok = False; break
        lst.append(a); a += i.size
    if ok and a == hi:
        spec_ok.append((lo, hi))
        for x in lst: allc[x] = True
    else:
        print('hueco NO codigo %06x-%06x' % (lo, hi))

def ext_len(op, size):
    am = op.address_mode
    if op.type == M.M68K_OP_REG: return 0
    if am in (M.M68K_AM_REG_DIRECT_DATA, M.M68K_AM_REG_DIRECT_ADDR, M.M68K_AM_REGI_ADDR,
              M.M68K_AM_REGI_ADDR_POST_INC, M.M68K_AM_REGI_ADDR_PRE_DEC, M.M68K_AM_NONE): return 0
    if am in (M.M68K_AM_REGI_ADDR_DISP, M.M68K_AM_AREGI_INDEX_8_BIT_DISP, M.M68K_AM_PCI_DISP,
              M.M68K_AM_PCI_INDEX_8_BIT_DISP, M.M68K_AM_ABSOLUTE_DATA_SHORT,
              M.M68K_AM_AREGI_INDEX_BASE_DISP, M.M68K_AM_PCI_INDEX_BASE_DISP): return 2
    if am == M.M68K_AM_ABSOLUTE_DATA_LONG: return 4
    if am == M.M68K_AM_IMMEDIATE: return 4 if size == 4 else 2
    return None

patches = {}   # offset -> (old, new, kind, insn)
odd = []
stats = {}
for a in sorted(allc):
    i = next(md.disasm(D[a:a+10], a))
    mn = i.mnemonic
    sz = {'b': 1, 'w': 2, 'l': 4}.get(mn.split('.')[-1] if '.' in mn else '', 2)
    if mn.startswith(('lea', 'pea', 'jmp', 'jsr', 'movea.l', 'cmpa.l', 'adda.l', 'suba.l')): pass
    base = mn.split('.')[0]
    if base in ('bra','bsr') or (base.startswith('b') and len(base)==3 and base not in ('bset','bclr','bchg','btst')) or base.startswith('db'):
        continue
    off = 2
    if base == 'movem':
        off = 4
    if base in ('btst', 'bset', 'bclr', 'bchg') and i.operands[0].address_mode == M.M68K_AM_IMMEDIATE:
        # numero de bit en palabra de extension
        off = 4
        ops = i.operands[1:]
    else:
        ops = list(i.operands)
    if base == 'movem':
        ops = [o for o in ops if o.type != M.M68K_OP_REG_BITS]
    total = 0
    lay = []
    good = True
    QUICK = ('addq','subq','moveq','lsl','lsr','asl','asr','rol','ror','roxl','roxr','trap')
    for o in ops:
        L = ext_len(o, sz)
        if base in QUICK and o.address_mode == M.M68K_AM_IMMEDIATE: L = 0
        if L is None: good = False; break
        lay.append((o, off + total, L)); total += L
    if not good or off + total != i.size:
        odd.append('%06x %s %s size=%d calc=%d' % (a, mn, i.op_str, i.size, off + total))
        continue
    for o, p, L in lay:
        if L != 4: continue
        v = struct.unpack('>I', D[a+p:a+p+4])[0]
        nv, n = remap(v)
        if nv is None: continue
        kind = 'abs' if o.address_mode == M.M68K_AM_ABSOLUTE_DATA_LONG else 'imm'
        patches[a + p] = (v, nv, kind + ':' + n, '%06x %s %s' % (a, mn, i.op_str))
        stats[kind + ':' + n] = stats.get(kind + ':' + n, 0) + 1
print('codigo', len(allc), 'parches', len(patches), stats)
print('raros', len(odd)); print('\n'.join(odd[:40]))
for k, (v, nv, kind, t) in sorted(patches.items()):
    if not kind.endswith('work') and not kind.startswith('abs'): print('IMM', kind, t)
pickle.dump({'patches': patches, 'code': sorted(allc)}, open('reloc.pkl', 'wb'))
