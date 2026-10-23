#!/usr/bin/env python3
"""mkrom.py - build the Thunder Hoop Neo Geo P ROM from the arcade 68k program plus the port runtime.

    mkrom.py stubs <th_cpu.bin> <vram_sites.txt> <out stubs.s>
    mkrom.py build <th_cpu.bin> <analysis dir> <game.bin> <game.sym> <out p1>

analysis dir: absrefs.csv (static_dasm.py seeded with the coverage run and sweep.py's linear sweep),
code_ranges.csv (static_dasm.py with the coverage seeds only: code for sure), swept_ranges.csv (sweep.py:
code very likely, or data that decodes as code), trap_tables.csv, vram_sites.txt.
What it does (addresses: analysis/INFORME_68K.md):
  1. relocates every absolute reference to the arcade work RAM / palette / sprites / I/O / video registers
     into Neo Geo work RAM (layout in src/ram.i), in the instructions and in the data tables of the
     code area (RAM pointers of the menus, the protection's pointer lists);
  2. VRAM / screen RAM write sites: a constant written with move.l #imm / move.w #imm gets its value
     decrypted at build time; every other site becomes an A-line opcode whose stub (stubs.s) decrypts
     at run time (src/crypt.s);
  3. patches: reset prologue (stack), interrupt masks (the arcade VBlank is level 6, the Neo Geo's 1),
     the speed calibration of the timing protection;
  4. overlays the runtime: vectors + header at 0, code at RUNTIME_ORG, P2 tables at 0x200000.
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis'))
import gaelcrypt  # noqa: E402
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000  # noqa: E402

RUNTIME_ORG = 0xB0000
CODE_HI = 0x26000                               # the program code ends below here (maps / graphics data above)
RELOC = [  # (lo, hi, new lo)
    (0xFF8000, 0x1000000, 0x104000),            # work RAM
    (0x200000, 0x200800, 0x10C000),             # palette
    (0x440000, 0x441000, 0x10C800),             # sprites
    (0x700000, 0x700060, 0x10D800),             # I/O
    (0x108000, 0x108010, 0x10D860),             # video registers / IRQ ack
]
VRAM_LO, VRAM_HI = 0x100000, 0x104000


def be32(b, o): return struct.unpack('>I', bytes(b[o:o + 4]))[0]
def put32(b, o, v): b[o:o + 4] = struct.pack('>I', v & 0xFFFFFFFF)
def put16(b, o, v): b[o:o + 2] = struct.pack('>H', v & 0xFFFF)


def reloc(v):
    for lo, hi, new in RELOC:
        if lo <= v < hi:
            return v - lo + new
    return None


cs = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)


def dis(rom, pc):
    return next(cs.disasm(bytes(rom[pc:pc + 12]), pc, 1))


def parse(op):
    """capstone operand -> (kind, reg / value, extra)"""
    op = op.strip()
    if op[0] in 'da' and len(op) == 2:
        return ('reg', op, None)
    if op.startswith('#$'):
        return ('imm', int(op[2:], 16), None)
    if op.startswith('$') and op.endswith('.l'):
        return ('abs', int(op[1:-2], 16), None)
    if op.startswith('(') and op.endswith(')+'):
        return ('postinc', op[1:3], None)
    if op.startswith('(') and op.endswith(')') and ',' in op:
        an, idx = [x.strip() for x in op[1:-1].split(',')]
        return ('index', an, idx)
    if op.startswith('(') and op.endswith(')'):
        return ('ind', op[1:3], None)
    if op.startswith('$') and '(' in op:
        d, an = op.split('(')
        return ('disp', an[:2], int(d[1:], 16))
    raise ValueError('operand ' + op)


def frame(r):
    """offset of the saved register in the line-A frame"""
    assert r != 'a7' and r != 'sp', r
    return (0 if r[0] == 'd' else 32) + 4 * int(r[1])


def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    return [x.strip() for x in out if x.strip()]


def sites(rom, path):
    """[(pc, insn, action)] action = ('imm', new bytes offset, value) or ('stub', n)"""
    res = []
    n = 0
    for line in open(path):
        line = line.split('#')[0].strip()
        if not line:
            continue
        pc = int(line.split()[0], 16)
        i = dis(rom, pc)
        ops = split_ops(i.op_str)
        # (constants are decrypted at run time too: the game checks some of these immediates in ROM)
        res.append((pc, i, ('stub', n)))
        n += 1
    return res


def gen_stub(pc, i):
    m = i.mnemonic
    ops = split_ops(i.op_str)
    out = ['s_%06X:                         /* %06X: %s %s */' % (pc, pc, m, i.op_str)]
    if m == 'clr.l':
        # a read-modify-write: the 68000 writes the low word first, so the hardware sees two unpaired
        # first words (decrypt(0, 0, 0) twice), not a move.l pair
        dst = parse(ops[0])
        z = gaelcrypt.decrypt(0, 0, 0)
        out.append('        move.l  #0x%04X%04X,d0' % (z, z))
        out.append('        moveq   #0,d7')
    else:
        assert m in ('move.l', 'move.w'), (hex(pc), m)
        src, dst = parse(ops[0]), parse(ops[1])
        k, r, x = src
        if k == 'reg':
            out.append('        move.l  %d(sp),d0' % frame(r))
        elif k == 'imm':
            out.append('        move.l  #0x%X,d0' % r)
        elif k == 'abs':
            v = reloc(r)
            out.append('        move.l  0x%X,d0' % (v if v is not None else r))
        elif k in ('ind', 'postinc'):
            out.append('        movea.l %d(sp),a1' % frame(r))
            out.append('        move.l  (a1),d0')
            if k == 'postinc':
                out.append('        addq.l  #4,%d(sp)' % frame(r))
        else:
            raise ValueError('%06X src %s' % (pc, ops[0]))
    if m == 'move.w':
        # a lone word: always a first word for the hardware
        assert src[0] == 'imm' and dst[0] == 'abs', hex(pc)
        out.append('        move.w  #0x%04X,d0' % gaelcrypt.decrypt(0, 0, src[1]))
        out.append('        moveq   #0,d7')
        out.append('        move.w  #0x%04X,d7' % src[1])
        out.append('        lea     0x%X,a1' % src[1] if False else '        lea     0x%X,a1' % dst[1])
        out.append('        move.w  d0,(a1)')
        out.append('        moveq   #%d,d6' % i.size)
        out.append('        jmp     linea_done')
        return out
    if m != 'clr.l':
        out.append('        move.l  d0,d7')
        out.append('        jsr     decrypt_long')
    k, r, x = dst
    if k == 'abs':
        assert VRAM_LO <= r < VRAM_HI, hex(pc)
        out.append('        lea     0x%X,a1' % r)
    elif k in ('ind', 'postinc'):
        out.append('        movea.l %d(sp),a1' % frame(r))
        if k == 'postinc':
            out.append('        addq.l  #4,%d(sp)' % frame(r))
    elif k == 'index':
        out.append('        movea.l %d(sp),a1' % frame(r))
        reg, sz = x.split('.')
        out.append('        move.l  %d(sp),d1' % frame(reg))
        out.append('        adda.%s  d1,a1' % sz)
    elif k == 'disp':
        out.append('        movea.l %d(sp),a1' % frame(r))
        out.append('        lea     %d(a1),a1' % (x if x < 0x8000 else x - 0x10000))
    else:
        raise ValueError('%06X dst %s' % (pc, ops[1]))
    out.append('        jsr     vstore')
    out.append('        moveq   #%d,d6' % i.size)
    out.append('        jmp     linea_done')
    return out


def cmd_stubs(a):
    rom = open(a[0], 'rb').read()
    lst = sites(rom, a[1])
    lines = ['* stubs.s - generated by tools/mkrom.py: one stub per arcade VRAM write site (line A 0xA000 + n)',
             '        .text', '        .global linea_sites', '        .align  2', 'linea_sites:']
    stubs = [(pc, i) for pc, i, act in lst if act[0] == 'stub']
    lines += ['        .long   s_%06X' % pc for pc, i in stubs]
    for pc, i in stubs:
        lines += gen_stub(pc, i)
    open(a[2], 'w').write('\n'.join(lines) + '\n')
    print('stubs: %d sites, %d constants decrypted at build time' % (len(stubs), len(lst) - len(stubs)))


def cmd_build(a):
    rom = bytearray(open(a[0], 'rb').read())
    adir = a[1]
    rt = open(a[2], 'rb').read()
    syms = {}
    for line in open(a[3]):
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    out = a[4]
    orig = bytes(rom)

    code = set()
    for name in ('code_ranges.csv', 'swept_ranges.csv'):
        for row in csv.DictReader(open(os.path.join(adir, name))):
            code.update(range(int(row['start'], 16), int(row['end'], 16)))
    tables = set()
    for row in csv.DictReader(open(os.path.join(adir, 'trap_tables.csv'))):
        t0 = int(row['start'], 16)
        tables.update(range(t0, t0 + 4 * int(row['entries'])))

    # 1a. instruction operands
    done = set()
    nins = 0
    for row in csv.DictReader(open(os.path.join(adir, 'absrefs.csv'))):
        if row['kind'] not in ('absL', 'imm32'):
            continue
        o = int(row['pc'], 16) + int(row['operand_offset'])
        if o in done:
            continue
        v = be32(orig, o)
        if v == 0x00FFFFFF and row['mnemonic'].split('.')[0] in ('andi', 'ori', 'eori'):
            continue
        nv = reloc(v & 0xFFFFFF) if v >> 24 in (0, 0xFF) else None     # lea $ffff8000.l (RAM test)
        if nv is None:
            continue
        put32(rom, o, nv)
        done.update(range(o, o + 4))
        nins += 1
    # 1b. RAM pointers (00FFxxxx) in the data of the code area: menu tables, object scripts (the stack
    #     page FFF000- is left out: coordinates such as 00 FF F3 FF).  The scripts also hold 24-bit RAM
    #     pointers after a parameter byte (01 FF E1 48): those are relocated where the interpreter masks
    #     them (andi.l #$FFFFFF, step 3)
    ndat = 0
    other = []
    for o in range(0x400, CODE_HI, 2):
        if any(x in code or x in tables or x in done for x in range(o, o + 4)):
            continue
        v = be32(orig, o)
        if 0xFF8000 <= v < 0xFFF000:
            put32(rom, o, reloc(v))
            done.update(range(o, o + 4))
            ndat += 1
        elif reloc(v) is not None and v & 0xFFFF:
            other.append((o, v))
    print('relocated: %d instruction operands, %d data pointers' % (nins, ndat))
    if other:
        print('not relocated (palette / sprite / I/O values in data, check):',
              ' '.join('%X:%08X' % x for x in other[:40]))

    # 2. VRAM write sites
    lst = sites(orig, os.path.join(adir, 'vram_sites.txt'))
    for pc, i, act in lst:
        if act[0] == 'imm':
            if i.mnemonic == 'move.l':
                v = be32(orig, pc + 2)
                put32(rom, pc + 2, gaelcrypt.decrypt_long(v))
            else:
                v = struct.unpack('>H', orig[pc + 2:pc + 4])[0]
                put16(rom, pc + 2, gaelcrypt.decrypt(0, 0, v))
        else:
            put16(rom, pc, 0xA000 | act[1])
    print('VRAM sites: %d (%d A-line stubs)' % (len(lst), sum(1 for x in lst if x[1] and x[2][0] == 'stub')))

    # 3. patches
    assert orig[0x400:0x410] == bytes.fromhex('203C00000000 46FC2700 4FF80000 2F00'.replace(' ', '')), 'reset'
    rom[0x400:0x410] = bytes.fromhex('7000 4E71 4E71 4EF9'.replace(' ', '')) + struct.pack('>I', syms['rt_reset']) + \
        bytes.fromhex('4E714E71')
    # the only SR writes of the program that lower the mask (to 5: VBlank on): 0 instead
    assert orig[0x48A:0x48E] == bytes.fromhex('46FC2500')         # move.w #$2500,sr
    put16(rom, 0x48C, 0x2000)
    for pc in (0x4BC, 0x4C2):
        assert orig[pc:pc + 4] == bytes.fromhex('027CF5FF')        # andi.w #$f5ff,sr
        put16(rom, pc + 2, 0xF0FF)
    assert orig[0x4AE:0x4B4] == bytes.fromhex('4BF900FF8000'), 'main loop head'
    rom[0x4AE:0x4B4] = b'\x4E\xB9' + struct.pack('>I', syms['rt_fixcal'])

    assert orig[0x1EC08:0x1EC0E] == bytes.fromhex('23C400FFE558'), 'checksum store'
    rom[0x1EC08:0x1EC0E] = b'\x4E\xB9' + struct.pack('>I', syms['rt_cksum'])
    cksum = sum(orig[0x400:0x7FFFF]) & 0xFFFFFFFF
    assert orig[0x106D2:0x106D8] == bytes.fromhex('363900FFD2D8'), 'sprite list update'
    rom[0x106D2:0x106D8] = b'\x4E\xB9' + struct.pack('>I', syms['rt_sprdone'])

    # the script interpreter's 24-bit pointers: andi.l #$FFFFFF,dn -> jsr rt_m24_dn (masks and relocates)
    nm = 0
    for o in range(0x400, CODE_HI, 2):
        w = struct.unpack('>H', orig[o:o + 2])[0]
        if w & 0xFFF8 == 0x0280 and orig[o + 2:o + 6] == bytes.fromhex('00FFFFFF') and o in code:
            n = w & 7
            assert n <= 1, hex(o)
            rom[o:o + 6] = b'\x4E\xB9' + struct.pack('>I', syms['rt_m24_d%d' % n])
            nm += 1
    print('24-bit pointer masks hooked: %d' % nm)

    # 4. runtime overlay
    assert rt[0x100:0x107] == b'NEO-GEO'
    vend = syms['__vectors_end']
    assert vend <= 0x400, hex(vend)
    rom[0:vend] = rt[0:vend]
    tlo, thi = syms['__text_start'], syms['__text_end']
    assert tlo == RUNTIME_ORG and thi <= 0x100000, hex(thi)
    assert all(b in (0, 0xFF) for b in orig[tlo:thi]), 'runtime over arcade data'
    rom[tlo:thi] = rt[tlo:thi]
    assert rom[syms['rt_cksum']:syms['rt_cksum'] + 6] == bytes.fromhex('283C12345678'), 'rt_cksum'
    put32(rom, syms['rt_cksum'] + 2, cksum)
    p2 = bytearray(rt[0x200000:]) if len(rt) > 0x200000 else bytearray()
    p2 += bytes(0x100000 - len(p2))
    print('runtime: %X-%X (%d bytes), P2 tables %d bytes' % (tlo, thi, thi - tlo, len(rt) - 0x200000))
    put32(rom, 0x10A, 0x200000)
    open(out, 'wb').write(bytes(rom) + bytes(p2))
    print('wrote', out, len(rom) + len(p2), 'bytes')


if __name__ == '__main__':
    {'stubs': cmd_stubs, 'build': cmd_build}[sys.argv[1]](sys.argv[2:])
