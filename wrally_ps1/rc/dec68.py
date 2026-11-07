"""Decodificador 68000 (subconjunto entero) para el recompilador estatico de World Rally."""
import struct

class EA:
    __slots__ = ('m', 'r', 'v', 'x')
    # m: 'D','A','ind','post','pre','d16','idx','absw','absl','pcd','pci','imm'
    # r: registro base; v: desplazamiento / direccion / inmediato; x: (tipo 'D'/'A', n, long) del indice
    def __init__(self, m, r=0, v=0, x=None):
        self.m, self.r, self.v, self.x = m, r, v, x
    def __repr__(self):
        m, r, v = self.m, self.r, self.v
        if m == 'D': return 'd%d' % r
        if m == 'A': return 'a%d' % r
        if m == 'ind': return '(a%d)' % r
        if m == 'post': return '(a%d)+' % r
        if m == 'pre': return '-(a%d)' % r
        if m == 'd16': return '%d(a%d)' % (v, r)
        if m == 'idx': return '%d(a%d,%s%d.%s)' % (v, r, self.x[0].lower(), self.x[1], 'l' if self.x[2] else 'w')
        if m == 'absw' or m == 'absl': return '$%x' % v
        if m == 'pcd': return '$%x(pc)' % v
        if m == 'pci': return '$%x(pc,%s%d.%s)' % (v, self.x[0].lower(), self.x[1], 'l' if self.x[2] else 'w')
        if m == 'imm': return '#$%x' % v
    def mem(self): return self.m not in ('D', 'A', 'imm')

class Ins:
    def __init__(self, a):
        self.a = a; self.op = None; self.sz = 0; self.src = None; self.dst = None
        self.cond = None; self.target = None; self.n = 0; self.size = 2; self.extra = None
    def __repr__(self):
        s = '%06x %s' % (self.a, self.op)
        if self.sz: s += '.' + {1: 'b', 2: 'w', 4: 'l'}[self.sz]
        if self.cond: s += '[%s]' % self.cond
        ops = [repr(o) for o in (self.src, self.dst) if o is not None]
        if self.target is not None: ops.append('$%x' % self.target)
        if self.extra is not None: ops.append(str(self.extra))
        return s + ' ' + ', '.join(ops)

CC = ['t', 'f', 'hi', 'ls', 'cc', 'cs', 'ne', 'eq', 'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le']

class Dec:
    def __init__(self, data):
        self.D = data
    def w(self, a): return struct.unpack('>H', self.D[a:a + 2])[0]
    def sw(self, a): return struct.unpack('>h', self.D[a:a + 2])[0]
    def l(self, a): return struct.unpack('>I', self.D[a:a + 4])[0]

    def ea(self, ins, mode, reg, sz):
        p = ins.a + ins.size
        if mode == 0: return EA('D', reg)
        if mode == 1: return EA('A', reg)
        if mode == 2: return EA('ind', reg)
        if mode == 3: return EA('post', reg)
        if mode == 4: return EA('pre', reg)
        if mode == 5:
            ins.size += 2; return EA('d16', reg, self.sw(p))
        if mode == 6:
            e = self.w(p); ins.size += 2
            d8 = e & 0xFF
            if d8 & 0x80: d8 -= 0x100
            return EA('idx', reg, d8, ('A' if e & 0x8000 else 'D', (e >> 12) & 7, bool(e & 0x800)))
        if mode == 7:
            if reg == 0:
                ins.size += 2; v = self.sw(p) & 0xFFFFFF; return EA('absw', 0, v)
            if reg == 1:
                ins.size += 4; return EA('absl', 0, self.l(p) & 0xFFFFFF)
            if reg == 2:
                ins.size += 2; return EA('pcd', 0, (p + self.sw(p)) & 0xFFFFFF)
            if reg == 3:
                e = self.w(p); ins.size += 2
                d8 = e & 0xFF
                if d8 & 0x80: d8 -= 0x100
                return EA('pci', 0, (p + d8) & 0xFFFFFF, ('A' if e & 0x8000 else 'D', (e >> 12) & 7, bool(e & 0x800)))
            if reg == 4:
                if sz == 4:
                    ins.size += 4; return EA('imm', 0, self.l(p))
                v = self.w(p); ins.size += 2
                return EA('imm', 0, v & 0xFF if sz == 1 else v)
        raise ValueError('EA %d %d en %06x' % (mode, reg, ins.a))

    def dec(self, a):
        i = Ins(a)
        op = self.w(a)
        hi = op >> 12
        m, r = (op >> 3) & 7, op & 7
        rx = (op >> 9) & 7
        ssz = [1, 2, 4, None][(op >> 6) & 3]
        if hi == 0:
            if op & 0x100:
                if m == 1: raise ValueError('movep %06x' % a)
                i.op = ['btst', 'bchg', 'bclr', 'bset'][(op >> 6) & 3]
                i.src = EA('D', rx); i.dst = self.ea(i, m, r, 1)
                i.sz = 4 if m == 0 else 1
                return i
            kind = (op >> 9) & 7
            if kind == 4:   # bits estaticos
                i.op = ['btst', 'bchg', 'bclr', 'bset'][(op >> 6) & 3]
                i.src = EA('imm', 0, self.w(a + 2) & 0xFF); i.size += 2
                i.dst = self.ea(i, m, r, 1)
                i.sz = 4 if m == 0 else 1
                return i
            names = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}
            if kind not in names: raise ValueError('op0 %04x %06x' % (op, a))
            i.op = names[kind]
            if (op & 0x3F) == 0x3C:    # a CCR / SR
                if ssz == 1: i.op += '_ccr'; i.src = EA('imm', 0, self.w(a + 2) & 0xFF); i.size += 2; return i
                if ssz == 2: i.op += '_sr'; i.src = EA('imm', 0, self.w(a + 2)); i.size += 2; return i
            i.sz = ssz
            i.src = self.ea(i, 7, 4, ssz)
            i.dst = self.ea(i, m, r, ssz)
            return i
        if hi in (1, 2, 3):
            sz = {1: 1, 3: 2, 2: 4}[hi]
            i.sz = sz
            i.src = self.ea(i, m, r, sz)
            dm = (op >> 6) & 7
            if dm == 1: i.op = 'movea'; i.dst = EA('A', rx); return i
            i.op = 'move'; i.dst = self.ea(i, dm, rx, sz)
            return i
        if hi == 4:
            if op == 0x4E75: i.op = 'rts'; return i
            if op == 0x4E73: i.op = 'rte'; return i
            if op == 0x4E71: i.op = 'nop'; return i
            if op == 0x4E77: i.op = 'rtr'; return i
            if (op & 0xFFC0) == 0x4E80: i.op = 'jsr'; i.dst = self.ea(i, m, r, 4); return i
            if (op & 0xFFC0) == 0x4EC0: i.op = 'jmp'; i.dst = self.ea(i, m, r, 4); return i
            if (op & 0xF1C0) == 0x41C0: i.op = 'lea'; i.sz = 4; i.src = self.ea(i, m, r, 4); i.dst = EA('A', rx); return i
            if (op & 0xFFC0) == 0x4840 and m == 0: i.op = 'swap'; i.sz = 4; i.dst = EA('D', r); return i
            if (op & 0xFFC0) == 0x4840: i.op = 'pea'; i.sz = 4; i.src = self.ea(i, m, r, 4); return i
            if (op & 0xFFF8) == 0x4880: i.op = 'ext'; i.sz = 2; i.dst = EA('D', r); return i
            if (op & 0xFFF8) == 0x48C0: i.op = 'ext'; i.sz = 4; i.dst = EA('D', r); return i
            if (op & 0xFB80) == 0x4880:   # movem
                i.op = 'movem'; i.sz = 4 if op & 0x40 else 2
                i.extra = self.w(a + 2); i.size += 2
                i.n = 1 if op & 0x400 else 0     # 1 = memoria -> registros
                e = self.ea(i, m, r, i.sz)
                if i.n: i.src = e
                else: i.dst = e
                return i
            if (op & 0xFFC0) == 0x40C0: i.op = 'move_from_sr'; i.sz = 2; i.dst = self.ea(i, m, r, 2); return i
            if (op & 0xFFC0) == 0x44C0: i.op = 'move_to_ccr'; i.sz = 2; i.src = self.ea(i, m, r, 2); return i
            if (op & 0xFFC0) == 0x46C0: i.op = 'move_to_sr'; i.sz = 2; i.src = self.ea(i, m, r, 2); return i
            if (op & 0xFF00) in (0x4000, 0x4200, 0x4400, 0x4600, 0x4A00) and ssz:
                i.op = {0x4000: 'negx', 0x4200: 'clr', 0x4400: 'neg', 0x4600: 'not', 0x4A00: 'tst'}[op & 0xFF00]
                i.sz = ssz; i.dst = self.ea(i, m, r, ssz); return i
            if (op & 0xFFC0) == 0x4AC0: i.op = 'tas'; i.sz = 1; i.dst = self.ea(i, m, r, 1); return i
            raise ValueError('op4 %04x %06x' % (op, a))
        if hi == 5:
            if (op & 0xC0) == 0xC0:
                cc = (op >> 8) & 15
                if m == 1:
                    i.op = 'dbcc'; i.cond = CC[cc]; i.dst = EA('D', r)
                    i.target = (a + 2 + self.sw(a + 2)) & 0xFFFFFF; i.size += 2; return i
                i.op = 'scc'; i.cond = CC[cc]; i.sz = 1; i.dst = self.ea(i, m, r, 1); return i
            i.op = 'subq' if op & 0x100 else 'addq'
            i.sz = ssz; q = rx or 8
            i.src = EA('imm', 0, q); i.dst = self.ea(i, m, r, ssz)
            if i.dst.m == 'A': i.sz = 4; i.op += 'a'
            return i
        if hi == 6:
            cc = (op >> 8) & 15
            d = op & 0xFF
            if d == 0: d = self.sw(a + 2); i.size += 2
            elif d & 0x80: d -= 0x100
            i.target = (a + 2 + d) & 0xFFFFFF
            i.op = 'bra' if cc == 0 else 'bsr' if cc == 1 else 'bcc'
            if cc > 1: i.cond = CC[cc]
            return i
        if hi == 7:
            i.op = 'moveq'; i.sz = 4; d = op & 0xFF
            i.src = EA('imm', 0, (d - 0x100 if d & 0x80 else d) & 0xFFFFFFFF); i.dst = EA('D', rx); return i
        if hi in (8, 9, 0xB, 0xC, 0xD):
            om = (op >> 6) & 7
            if hi == 8 and om in (3, 7):
                i.op = 'divu' if om == 3 else 'divs'; i.sz = 2; i.src = self.ea(i, m, r, 2); i.dst = EA('D', rx); return i
            if hi == 0xC and om in (3, 7):
                i.op = 'mulu' if om == 3 else 'muls'; i.sz = 2; i.src = self.ea(i, m, r, 2); i.dst = EA('D', rx); return i
            if hi in (9, 0xB, 0xD) and om in (3, 7):
                i.op = {9: 'suba', 0xB: 'cmpa', 0xD: 'adda'}[hi]; i.sz = 2 if om == 3 else 4
                i.src = self.ea(i, m, r, i.sz); i.dst = EA('A', rx); return i
            if hi == 0xC and (op & 0x130) == 0x100 and om in (5, 6):
                i.op = 'exg'; i.sz = 4
                mm = (op >> 3) & 0x1F
                if mm == 8: i.src, i.dst = EA('D', rx), EA('D', r)
                elif mm == 9: i.src, i.dst = EA('A', rx), EA('A', r)
                elif mm == 0x11: i.src, i.dst = EA('D', rx), EA('A', r)
                else: raise ValueError('exg %06x' % a)
                return i
            sz = [1, 2, 4][om & 3]
            if hi in (8, 0xC) and (om & 4) and m in (0, 1): raise ValueError('bcd %06x' % a)
            if hi in (9, 0xD) and (om & 4) and m in (0, 1):
                i.op = 'subx' if hi == 9 else 'addx'; i.sz = sz
                if m == 0: i.src, i.dst = EA('D', r), EA('D', rx)
                else: i.src, i.dst = EA('pre', r), EA('pre', rx)
                return i
            if hi == 0xB:
                if om & 4:
                    if m == 1: i.op = 'cmpm'; i.sz = sz; i.src = EA('post', r); i.dst = EA('post', rx); return i
                    i.op = 'eor'; i.sz = sz; i.src = EA('D', rx); i.dst = self.ea(i, m, r, sz); return i
                i.op = 'cmp'; i.sz = sz; i.src = self.ea(i, m, r, sz); i.dst = EA('D', rx); return i
            i.op = {8: 'or', 9: 'sub', 0xC: 'and', 0xD: 'add'}[hi]; i.sz = sz
            if om & 4: i.src = EA('D', rx); i.dst = self.ea(i, m, r, sz)
            else: i.src = self.ea(i, m, r, sz); i.dst = EA('D', rx)
            return i
        if hi == 0xE:
            if (op & 0xC0) == 0xC0:   # en memoria, 1 bit
                t = (op >> 9) & 3
                i.op = ['as', 'ls', 'rox', 'ro'][t] + ('l' if op & 0x100 else 'r')
                i.sz = 2; i.src = EA('imm', 0, 1); i.dst = self.ea(i, m, r, 2); return i
            t = (op >> 3) & 3
            i.op = ['as', 'ls', 'rox', 'ro'][t] + ('l' if op & 0x100 else 'r')
            i.sz = ssz
            if op & 0x20: i.src = EA('D', rx)
            else: i.src = EA('imm', 0, rx or 8)
            i.dst = EA('D', r)
            return i
        raise ValueError('op %04x %06x' % (op, a))

if __name__ == '__main__':
    import pickle
    import sys, os
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
    from paths import rom, work
    D = open(work('prog.bin'), 'rb').read()
    code = {int(l, 16): None for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'code_addrs.txt')) if l.strip()}
    d = Dec(D); bad = 0
    for a in sorted(code):
        try: i = d.dec(a)
        except Exception as e: print('ERR', hex(a), e); bad += 1; continue
    print('ok', len(code), 'errores', bad)
