"""m68k_mini.py - the few 68000 forms the Thunder Hoop build needs, in pure Python (no capstone, no assembler),
so that tools/thng_build.py can run in a browser.

decode(rom, pc): the VRAM write instructions of the arcade program (move.l / move.w / clr.l to screen RAM),
    -> object with .mnemonic, .op_str (capstone's text: "d0, (a1)+", "$ffe0c4.l, $1024b0.l", "#$45c845c, ...")
    and .size.
assemble(lines, base, syms): the lines tools/mkrom.py gen_stub writes (stubs.s) -> (bytes, {label: address}).
"""
import re
import struct


class Insn:
    def __init__(self, mnemonic, op_str, size):
        self.mnemonic, self.op_str, self.size = mnemonic, op_str, size


def _ea(rom, pc, mode, reg, size_l, off):
    """effective address text and the bytes it takes after the opcode word"""
    if mode == 0:
        return 'd%d' % reg, 0
    if mode == 1:
        return 'a%d' % reg, 0
    if mode == 2:
        return '(a%d)' % reg, 0
    if mode == 3:
        return '(a%d)+' % reg, 0
    if mode == 6:
        ext = struct.unpack('>H', rom[off:off + 2])[0]
        assert ext & 0x8100 == 0x0000 or ext & 0x0100 == 0, 'full extension word at %X' % pc
        assert ext & 0xFF == 0, 'index displacement at %X' % pc
        return '(a%d, %s%d.%s)' % (reg, 'a' if ext & 0x8000 else 'd', (ext >> 12) & 7, 'l' if ext & 0x800 else 'w'), 2
    if mode == 7 and reg == 1:
        return '$%x.l' % struct.unpack('>I', rom[off:off + 4])[0], 4
    if mode == 7 and reg == 4:
        if size_l:
            return '#$%x' % struct.unpack('>I', rom[off:off + 4])[0], 4
        return '#$%x' % struct.unpack('>H', rom[off:off + 2])[0], 2
    raise ValueError('addressing mode %d/%d at %X' % (mode, reg, pc))


def decode(rom, pc):
    w = struct.unpack('>H', rom[pc:pc + 2])[0]
    if w == 0x42B9:                                             # clr.l abs.l
        return Insn('clr.l', '$%x.l' % struct.unpack('>I', rom[pc + 2:pc + 6])[0], 6)
    if w >> 12 in (2, 3):                                       # move.l / move.w
        long = w >> 12 == 2
        src, n1 = _ea(rom, pc, (w >> 3) & 7, w & 7, long, pc + 2)
        dst, n2 = _ea(rom, pc, (w >> 6) & 7, (w >> 9) & 7, long, pc + 2 + n1)
        return Insn('move.l' if long else 'move.w', '%s, %s' % (src, dst), 2 + n1 + n2)
    raise ValueError('instruction %04X at %X' % (w, pc))


_REG = re.compile(r'^([da])([0-7])$')


def _imm(s, syms):
    s = s.strip()
    return syms[s] if s in syms else int(s, 0)


def _enc(line, syms):
    """one stub line -> bytes"""
    m = re.match(r'^(\w+(?:\.\w)?)\s*(.*)$', line.strip())
    op, args = m.group(1), [a.strip() for a in m.group(2).split(',')] if m.group(2) else []
    if op == 'moveq':
        return struct.pack('>H', 0x7000 | int(args[1][1]) << 9 | (_imm(args[0][1:], syms) & 0xFF))
    if op in ('jsr', 'jmp'):
        return struct.pack('>HI', 0x4EB9 if op == 'jsr' else 0x4EF9, _imm(args[0], syms))
    if op == 'lea':                                             # lea abs.l,a1
        assert args[1] == 'a1', line
        return struct.pack('>HI', 0x43F9, _imm(args[0], syms))
    if op == 'adda.w':                                          # adda.w d1,a1
        assert args == ['d1', 'a1'], line
        return struct.pack('>H', 0xD2C1)
    if op == 'adda.l':
        assert args == ['d1', 'a1'], line
        return struct.pack('>H', 0xD3C1)
    if op == 'movea.l':                                         # movea.l d(sp),a1
        d = re.match(r'^(-?\d+)\(sp\)$', args[0])
        assert d and args[1] == 'a1', line
        if int(d.group(1)) == 0:                                # (sp): gas drops a zero displacement
            return struct.pack('>H', 0x2257)
        return struct.pack('>Hh', 0x226F, int(d.group(1)))
    if op == 'addq.l':                                          # addq.l #n,d(sp)
        d = re.match(r'^(-?\d+)\(sp\)$', args[1])
        if int(d.group(1)) == 0:
            return struct.pack('>H', 0x5097 | (int(args[0][1:]) & 7) << 9)
        return struct.pack('>Hh', 0x50AF | (int(args[0][1:]) & 7) << 9, int(d.group(1)))
    if op == 'move.w':
        if args[0] == 'd0' and args[1] == '(a1)':
            return struct.pack('>H', 0x3280)
        dn = _REG.match(args[1])
        assert args[0].startswith('#') and dn and dn.group(1) == 'd', line
        return struct.pack('>HH', 0x303C | int(dn.group(2)) << 9, _imm(args[0][1:], syms) & 0xFFFF)
    if op == 'move.l':
        dn = _REG.match(args[1])
        assert dn and dn.group(1) == 'd', line
        x = int(dn.group(2)) << 9
        if args[0].startswith('#'):
            v = _imm(args[0][1:], syms) & 0xFFFFFFFF
            if v < 0x80 or v >= 0xFFFFFF80:                     # gas turns it into moveq
                return struct.pack('>H', 0x7000 | x | v & 0xFF)
            return struct.pack('>HI', 0x203C | x, v)
        if args[0] == '(a1)':
            return struct.pack('>H', 0x2011 | x)
        d = re.match(r'^(-?\d+)\(sp\)$', args[0])
        if d:
            if int(d.group(1)) == 0:
                return struct.pack('>H', 0x2017 | x)
            return struct.pack('>Hh', 0x202F | x, int(d.group(1)))
        s = _REG.match(args[0])
        if s:
            assert s.group(1) == 'd', line
            return struct.pack('>H', 0x2000 | x | int(s.group(2)))
        return struct.pack('>HI', 0x2039 | x, _imm(args[0], syms))        # move.l abs.l,dn
    raise ValueError('stub line: ' + line)


def assemble(lines, base, syms):
    """stubs.s lines (labels, .long label, instructions) at address base -> (bytes, labels)"""
    items = []                                                  # ('label', name) / ('long', name) / ('code', text)
    for line in lines:
        s = re.sub(r'/\*.*?\*/', '', line).strip()
        if not s or s.startswith('*') or s in ('.text',) or s.startswith(('.global', '.align')):
            continue
        if s.endswith(':'):
            items.append(('label', s[:-1]))
        elif s.startswith('.long'):
            items.append(('long', s.split(None, 1)[1].strip()))
        else:
            m = re.match(r'^(s_[0-9A-Fa-f]+):\s*(.*)$', s)
            if m:
                items.append(('label', m.group(1)))
                s = m.group(2).strip()
                if not s:
                    continue
            items.append(('code', s))
    labels, addr = {}, base
    for kind, v in items:                                       # pass 1: sizes (operands as 0)
        if kind == 'label':
            labels[v] = addr
        elif kind == 'long':
            addr += 4
        else:
            addr += len(_enc(v, _Zero(syms)))
    out = bytearray()
    full = dict(syms)
    full.update(labels)
    for kind, v in items:                                       # pass 2
        if kind == 'long':
            out += struct.pack('>I', full[v])
        elif kind == 'code':
            out += _enc(v, full)
    assert base + len(out) == addr
    return bytes(out), labels


class _Zero(dict):
    """symbol table that answers 0 for any symbol (sizing pass)"""
    def __init__(self, syms):
        super().__init__()

    def __contains__(self, k):
        return re.match(r'^[A-Za-z_]\w*$', k) is not None

    def __getitem__(self, k):
        return 0
