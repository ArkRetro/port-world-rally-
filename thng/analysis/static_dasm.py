#!/usr/bin/env python3
"""Desensamblado recursivo estático del 68000 de Thunder Hoop (capstone) + inventario de referencias absolutas.

uso: static_dasm.py [--seeds cov_execpc.txt] [--out dir]
Salidas (en --out, por defecto .):
  code_insns.txt   : pc tamaño mnemónico operandos   (todas las instrucciones alcanzadas)
  absrefs.csv      : inventario de operandos absolutos (abs.L/abs.W) + inmediatos/lea hacia zonas de hardware
  basereg.csv      : cargas de bases de hardware en registros de dirección y usos (An)/d(An) posteriores
  calls.csv        : jsr/bsr -> destino ; indirect.csv : jmp/jsr indirectos
  ranges_static.csv: rangos [ini,fin) CODE según el desensamblado
"""
import sys, csv, struct, argparse, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gaelcrypt
from capstone import *
from capstone.m68k import *

LAB = '/tmp/claude-0/-home-user-port-world-rally-/0e54570e-879c-581d-b19c-42bb81ddf7e3/scratchpad/thlab'
ap = argparse.ArgumentParser()
ap.add_argument('--seeds', action='append', default=[])
ap.add_argument('--out', default='.')
ap.add_argument('--rom', default=LAB + '/th_cpu.bin')
args = ap.parse_args()
rom = open(args.rom, 'rb').read()
ROMSZ = len(rom)

REGIONS = [
    ('ROM', 0x000000, 0x100000), ('VRAM', 0x100000, 0x104000), ('VREG', 0x108000, 0x108010),
    ('PAL', 0x200000, 0x200800), ('SPR', 0x440000, 0x441000), ('IO', 0x700000, 0x700010),
    ('RAM', 0xFF0000, 0x1000000),
]
def region(a):
    a &= 0xFFFFFF
    for n, s, e in REGIONS:
        if s <= a < e: return n
    return 'OTHER'

cs = Cs(CS_ARCH_M68K, CS_MODE_M68K_000); cs.detail = True

def dis1(a):
    if a < 0 or a + 2 > ROMSZ or a & 1: return None
    try:
        for i in cs.disasm(rom[a:a + 12], a, 1):
            try:
                i.operands
            except CsError:
                return None
            return i
    except CsError:
        return None
    return None

FLOW_END = {'rts', 'rte', 'rtr', 'rtd', 'jmp', 'bra', 'illegal', 'stop', 'reset'}
COND = {'bhi','bls','bcc','bcs','bne','beq','bvc','bvs','bpl','bmi','bge','blt','bgt','ble','bsr',
        'dbt','dbf','dbra','dbhi','dbls','dbcc','dbcs','dbne','dbeq','dbvc','dbvs','dbpl','dbmi','dbge','dblt','dbgt','dble'}

def mn_base(i):
    m = i.mnemonic
    return m.split('.')[0]

def branch_target(i):
    for o in i.operands:
        if o.type == M68K_OP_BR_DISP:
            return (i.address + 2 + o.br_disp.disp) & 0xFFFFFF
    return None

def abs_target(i):
    """destino de jmp/jsr absoluto"""
    for o in i.operands:
        if o.type == M68K_OP_MEM and o.address_mode in (M68K_AM_ABSOLUTE_DATA_LONG, M68K_AM_ABSOLUTE_DATA_SHORT):
            return o.imm & 0xFFFFFF
        if o.type == M68K_OP_MEM and o.address_mode == M68K_AM_PCI_DISP:
            return (i.address + 2 + o.mem.disp) & 0xFFFFFF
    return None

# ---------- recursivo ----------
insns = {}      # addr -> insn
tables = {}     # trap #1 inline table start -> entries
seeds = []
for v in range(1, 64):
    t = struct.unpack('>I', rom[v * 4:v * 4 + 4])[0]
    if t < ROMSZ and not (t & 1): seeds.append(t)
exec_set = set()
for sf in args.seeds:
    for line in open(sf):
        p = line.split()
        if p:
            a = int(p[0], 16)
            if a < ROMSZ: exec_set.add(a)
    seeds += sorted(exec_set)
calls = []      # (pc, target, mnemonic)
indirect = []   # (pc, text)
bad = []        # seeds that fail to decode
work = list(seeds)
seen_seed = set()
while work:
    a = work.pop()
    while True:
        if a in insns or a >= ROMSZ or a & 1: break
        i = dis1(a)
        if i is None:
            bad.append(a); break
        insns[a] = i
        m = mn_base(i)
        if m in ('jsr', 'bsr'):
            t = abs_target(i) if m == 'jsr' else branch_target(i)
            if t is not None:
                calls.append((a, t, i.mnemonic)); work.append(t)
            else:
                indirect.append((a, i.mnemonic + ' ' + i.op_str))
        elif m in COND:
            t = branch_target(i)
            if t is not None: work.append(t)
        elif m in ('jmp', 'bra'):
            t = branch_target(i) if m == 'bra' else abs_target(i)
            if t is not None: work.append(t)
            else: indirect.append((a, i.mnemonic + ' ' + i.op_str))
        elif m == 'trap':
            n = i.operands[0].imm
            if n == 1:
                # trap #1 = switch(d0) over an inline table of encrypted code pointers (decrypted by the
                # handler at 0x506 through screen RAM): end of flow, the targets are new seeds
                t0, k, tg = a + 2, 0, set()
                while True:
                    ad = t0 + 4 * k
                    if ad in tg or ad + 4 > ROMSZ: break
                    v = gaelcrypt.decrypt_long(struct.unpack('>I', rom[ad:ad + 4])[0])
                    if v & 1 or not (0x400 <= v < 0x30000): break
                    tg.add(v); k += 1
                tables[t0] = k
                for x in sorted(tg):
                    calls.append((a, x, 'trap1')); work.append(x)
                break
            if n in (3, 5):
                a += 4                  # one inline parameter word (handlers 0x5AA / 0x646)
                continue
        if m in FLOW_END: break
        a += i.size

# ---------- rangos ----------
addrs = sorted(insns)
ranges = []
for a in addrs:
    e = a + insns[a].size
    if ranges and ranges[-1][1] >= a:
        ranges[-1][1] = max(ranges[-1][1], e)
    else:
        ranges.append([a, e])
out = args.out.rstrip('/') + '/'
with open(out + 'ranges_static.csv', 'w') as f:
    f.write('start,end,len\n')
    for s, e in ranges: f.write('%06X,%06X,%d\n' % (s, e, e - s))
with open(out + 'code_insns.txt', 'w') as f:
    for a in addrs:
        i = insns[a]; f.write('%06X %d %s %s\n' % (a, i.size, i.mnemonic, i.op_str))
with open(out + 'trap_tables.csv', 'w') as f:
    f.write('start,entries\n')
    for t0 in sorted(tables): f.write('%06X,%d\n' % (t0, tables[t0]))
with open(out + 'calls.csv', 'w') as f:
    f.write('pc,target,mnemonic\n')
    for pc, t, m in sorted(calls): f.write('%06X,%06X,%s\n' % (pc, t, m))
with open(out + 'indirect.csv', 'w') as f:
    f.write('pc,insn\n')
    for pc, t in sorted(indirect): f.write('%06X,%s\n' % (pc, t))

# ---------- inventario de referencias absolutas ----------
def op_size(i):
    if i.op_size and i.op_size.type == 1:
        return {1: '.b', 2: '.w', 4: '.l'}.get(i.op_size.size, '')
    return ''

def ext_len(o, i):
    """longitud (bytes) de la extensión de un operando en el flujo de instrucción"""
    if o.type == M68K_OP_IMM:
        if mn_base(i) in ('addq','subq','moveq','asl','asr','lsl','lsr','rol','ror','roxl','roxr','trap','bkpt'): return 0
        if i.op_size and i.op_size.size == 4: return 4
        return 2
    if o.type == M68K_OP_BR_DISP:
        return 0 if o.br_disp.disp_size == 1 else (2 if o.br_disp.disp_size == 2 else 4)
    if o.type == M68K_OP_REG_BITS: return 0
    if o.type == M68K_OP_MEM:
        am = o.address_mode
        if am in (M68K_AM_REGI_ADDR, M68K_AM_REGI_ADDR_POST_INC, M68K_AM_REGI_ADDR_PRE_DEC): return 0
        if am in (M68K_AM_REGI_ADDR_DISP, M68K_AM_AREGI_INDEX_8_BIT_DISP, M68K_AM_PCI_DISP, M68K_AM_PCI_INDEX_8_BIT_DISP, M68K_AM_ABSOLUTE_DATA_SHORT): return 2
        if am == M68K_AM_ABSOLUTE_DATA_LONG: return 4
        return 2
    return 0

READ_ONLY = {'cmp','cmpi','cmpa','cmpm','tst','btst','chk','mulu','muls','divu','divs','move','movea','moveq','lea','pea',
             'add','adda','sub','suba','and','or','eor','jsr','jmp','movem','link','unlk'}
RMW = {'add','addi','addq','sub','subi','subq','and','andi','or','ori','eor','eori','neg','negx','not','bset','bclr','bchg',
       'asl','asr','lsl','lsr','rol','ror','roxl','roxr','addx','subx','abcd','sbcd','nbcd','tas','clr'}

def access_modes(i):
    """lista de modos de acceso por operando: r / w / rw / addr / x"""
    m = mn_base(i); n = len(i.operands)
    res = ['r'] * n
    if m in ('jsr', 'jmp'): return ['x'] * n
    if m in ('lea', 'pea'): return ['addr'] + ['w'] * (n - 1)
    if m == 'clr': return ['w'] * n
    if m in ('cmp','cmpi','cmpa','tst','btst','chk','cmpm'): return ['r'] * n
    if m in ('move','movea','moveq','movep') and n == 2: return ['r', 'w']
    if m == 'movem' and n == 2:
        # capstone: movem regs,ea  -> [REG_BITS, MEM] ; movem ea,regs -> [MEM, REG_BITS]
        if i.operands[0].type == M68K_OP_REG_BITS: return ['r', 'w']
        return ['r', 'w']
    if m in RMW and n >= 1:
        res[-1] = 'rw'
        if n == 2 and i.operands[1].type == M68K_OP_REG: res[1] = 'rw'
        return res
    if n == 2 and i.operands[1].type in (M68K_OP_MEM,):
        res[1] = 'rw'
    return res

abs_rows = []   # pc,size,opoff,addr,mnemonic,access,opsize,region,kind
imm_rows = []
for a in addrs:
    i = insns[a]
    m = mn_base(i)
    modes = access_modes(i)
    off = 2
    if m == 'movem': off = 4   # la máscara de registros va siempre justo tras el opcode
    # el inmediato de btst/bset... #n,ea va antes; capstone lo da como operando 0 → coherente con el orden de codificación
    for k, o in enumerate(i.operands):
        L = ext_len(o, i)
        if o.type == M68K_OP_MEM and o.address_mode in (M68K_AM_ABSOLUTE_DATA_LONG, M68K_AM_ABSOLUTE_DATA_SHORT):
            addr = o.imm & 0xFFFFFF
            reg = region(addr)
            kind = 'absL' if o.address_mode == M68K_AM_ABSOLUTE_DATA_LONG else 'absW'
            # autocomprobación: los bytes del operando deben contener la dirección
            if kind == 'absL':
                enc = struct.unpack('>I', rom[a + off:a + off + 4])[0] & 0xFFFFFF
            else:
                enc = struct.unpack('>h', rom[a + off:a + off + 2])[0] & 0xFFFFFF
            ok = (enc == addr)
            acc = modes[k]
            if m in ('jsr', 'jmp'): acc = 'x'
            abs_rows.append((a, i.size, off, addr, i.mnemonic, i.op_str, acc, op_size(i), reg, kind, ok))
        elif o.type == M68K_OP_IMM and (i.op_size and i.op_size.size == 4) and m not in ('bsr',):
            v = o.imm & 0xFFFFFFFF
            if v < 0x1000000 and region(v) != 'OTHER' and (v >= 0x100000 or (m in ('movea', 'move', 'adda', 'add', 'suba') and v >= 0x400)):
                imm_rows.append((a, i.size, off, v, i.mnemonic, i.op_str, 'imm', op_size(i), region(v), 'imm32', True))
        off += L

with open(out + 'absrefs.csv', 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['pc', 'insn_size', 'operand_offset', 'address', 'mnemonic', 'operands', 'access', 'op_size', 'region', 'kind', 'selfcheck'])
    for r in sorted(abs_rows + imm_rows):
        w.writerow(['%06X' % r[0], r[1], r[2], '%06X' % r[3], r[4], r[5], r[6], r[7], r[8], r[9], 'ok' if r[10] else 'MISMATCH'])

# ---------- bases en registros de dirección ----------
AREG = {M68K_REG_A0: 'a0', M68K_REG_A1: 'a1', M68K_REG_A2: 'a2', M68K_REG_A3: 'a3', M68K_REG_A4: 'a4', M68K_REG_A5: 'a5', M68K_REG_A6: 'a6', M68K_REG_A7: 'a7'}
def dest_areg(i):
    if len(i.operands) >= 1:
        o = i.operands[-1]
        if o.type == M68K_OP_REG and o.reg in AREG:
            return o.reg
    return None

base_rows = []
for a in addrs:
    i = insns[a]; m = mn_base(i)
    base = None; src = None
    if m == 'lea' and i.operands[0].type == M68K_OP_MEM and i.operands[0].address_mode in (M68K_AM_ABSOLUTE_DATA_LONG, M68K_AM_ABSOLUTE_DATA_SHORT):
        base = i.operands[0].imm & 0xFFFFFF; src = 'lea'
    elif m in ('movea',) and i.operands[0].type == M68K_OP_IMM:
        base = i.operands[0].imm & 0xFFFFFFFF; src = 'movea#'
    elif m in ('movea',) and i.operands[0].type == M68K_OP_MEM and i.operands[0].address_mode in (M68K_AM_ABSOLUTE_DATA_LONG,):
        base = None  # puntero leído de memoria: se lista aparte
    elif m == 'adda' and i.operands[0].type == M68K_OP_IMM and i.op_size and i.op_size.size == 4:
        base = i.operands[0].imm & 0xFFFFFFFF; src = 'adda#'
    if base is None or region(base) in ('OTHER',) or (region(base) == 'ROM' and src != 'lea'):
        continue
    if region(base) == 'ROM' and src == 'lea':
        # lea de ROM: interesante solo si apunta a datos (fuera de código)
        pass
    reg = dest_areg(i)
    if reg is None: continue
    # escaneo lineal de usos
    uses = []
    b = a + i.size
    steps = 0
    while b in insns and steps < 300:
        j = insns[b]; jm = mn_base(j)
        for k, o in enumerate(j.operands):
            # capstone: en (An)/(An)+/-(An) el registro va en o.reg; en d(An)/d(An,Xn) en o.mem.base_reg
            breg = o.reg if o.address_mode in (M68K_AM_REGI_ADDR, M68K_AM_REGI_ADDR_POST_INC, M68K_AM_REGI_ADDR_PRE_DEC) else o.mem.base_reg
            if o.type == M68K_OP_MEM and breg == reg and o.address_mode in (M68K_AM_REGI_ADDR, M68K_AM_REGI_ADDR_POST_INC, M68K_AM_REGI_ADDR_PRE_DEC, M68K_AM_REGI_ADDR_DISP, M68K_AM_AREGI_INDEX_8_BIT_DISP, M68K_AM_AREGI_INDEX_BASE_DISP):
                uses.append((b, j.mnemonic + ' ' + j.op_str, o.mem.disp, access_modes(j)[k], op_size(j)))
        if jm in ('rts', 'rte', 'jmp', 'bra'): break
        if dest_areg(j) == reg and jm not in ('adda', 'suba', 'addq', 'subq'): break
        if jm == 'movem' and j.operands[-1].type == M68K_OP_REG_BITS and (j.operands[-1].register_bits >> (8 + (reg - M68K_REG_A0))) & 1: break
        b += j.size; steps += 1
    base_rows.append((a, i.mnemonic + ' ' + i.op_str, AREG[reg], base, region(base), src, uses))

with open(out + 'basereg.csv', 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['pc', 'insn', 'reg', 'base', 'region', 'how', 'n_uses', 'uses(pc:insn:disp:access:size;...)'])
    for a, txt, reg, base, rg, src, uses in base_rows:
        w.writerow(['%06X' % a, txt, reg, '%06X' % base, rg, src, len(uses),
                    ';'.join('%06X:%s:%d:%s:%s' % u for u in uses)])

# punteros leídos de memoria a registros de dirección (movea.l $xxx.l, An) y jsr/jmp indirectos ya listados
tot = sum(e - s for s, e in ranges)
print('instrucciones=%d rangos=%d bytes_codigo=%d (%.1f%% de 1MB) seeds_malos=%d indirectos=%d' % (len(insns), len(ranges), tot, 100.0 * tot / ROMSZ, len(bad), len(indirect)))
print('absrefs=%d imm=%d bases=%d' % (len(abs_rows), len(imm_rows), len(base_rows)))
mm = [r for r in abs_rows if not r[10]]
print('selfcheck mismatches=%d' % len(mm), ['%06X' % r[0] for r in mm[:10]])
