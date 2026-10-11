import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from paths import rom, work
"""Recompilador estatico 68000 -> C para World Rally (port PS1, uso personal).
Genera una unica funcion C con una etiqueta por instruccion (goto para saltos directos, tabla de
despacho para rts/rte/jmp indirectos), banderas solo donde se usan (analisis de vida) y memoria en
palabras de 16 bits en orden nativo (byte a en a^1)."""
import pickle, struct, sys
from dec68 import Dec, EA, Ins

D = open(work('prog.bin'), 'rb').read()
code = {int(l, 16): None for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'code_addrs.txt')) if l.strip()}
dec = Dec(D)
INS = {a: dec.dec(a) for a in sorted(code)}
ADDRS = sorted(INS)
NEXT = {a: a + INS[a].size for a in ADDRS}

X, N, Z, V, C = 16, 8, 4, 2, 1
ALL = 31
NZVC = N | Z | V | C
COND_USE = {'t': 0, 'f': 0, 'hi': C | Z, 'ls': C | Z, 'cc': C, 'cs': C, 'ne': Z, 'eq': Z, 'vc': V, 'vs': V,
            'pl': N, 'mi': N, 'ge': N | V, 'lt': N | V, 'gt': N | V | Z, 'le': N | V | Z}
COND_EXPR = {'t': '1', 'f': '0', 'hi': '(!fC && !fZ)', 'ls': '(fC || fZ)', 'cc': '(!fC)', 'cs': '(fC)',
             'ne': '(!fZ)', 'eq': '(fZ)', 'vc': '(!fV)', 'vs': '(fV)', 'pl': '(!fN)', 'mi': '(fN)',
             'ge': '(fN == fV)', 'lt': '(fN != fV)', 'gt': '(!fZ && fN == fV)', 'le': '(fZ || fN != fV)'}

# region prevista de los accesos por registro de cada instruccion (lectura, escritura, lecturas de 32 bits
# alineadas a 4): work/regiones.txt, de rc/regiones.py (construir.sh). Sin el, todo va por el acceso general
PRED = {}
_rf = work('regiones.txt')
if os.path.exists(_rf):
    for l in open(_rf):
        p = l.split()
        if len(p) >= 3 and not l.startswith('#'): PRED[int(p[0], 16)] = tuple(None if x == '-' else x for x in p[1:3]) + (p[3:4] == ['a'],)

IRQ6 = struct.unpack('>I', D[0x78:0x7C])[0]
RESET_PC = struct.unpack('>I', D[4:8])[0]

# ---------------------------------------------------------------- flujo y uso de banderas
def uses_defs(i):
    o = i.op
    if o in ('move', 'moveq', 'tst', 'clr', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'not', 'ext', 'swap',
             'mulu', 'muls', 'rol', 'ror'):
        return 0, NZVC
    if o in ('add', 'addi', 'addq', 'sub', 'subi', 'subq', 'neg'): return 0, ALL
    if o in ('cmp', 'cmpi', 'cmpa', 'cmpm'): return 0, NZVC
    if o in ('addx', 'subx', 'negx'): return X | Z, ALL
    if o in ('divu', 'divs'): return 0, NZVC
    if o in ('btst', 'bchg', 'bclr', 'bset'): return 0, Z
    if o in ('asl', 'asr', 'lsl', 'lsr'):
        return 0, ALL      # (cuenta 0 en registro deja X igual: se supone que no pasa; lo vigila el banco de pruebas)
    if o in ('roxl', 'roxr'): return X, ALL
    if o in ('scc', 'dbcc', 'bcc'): return COND_USE[i.cond], 0
    if o in ('move_to_sr', 'rte', 'move_to_ccr'): return 0, ALL
    if o in ('ori_sr', 'andi_sr', 'eori_sr', 'ori_ccr', 'andi_ccr', 'eori_ccr'): return ALL, ALL
    if o == 'move_from_sr': return ALL, 0
    return 0, 0

def succs(i):
    """(sucesores conocidos, salida_indirecta)"""
    o = i.op; n = NEXT[i.a]
    if o == 'bra': return [i.target], False
    if o == 'bcc': return [n, i.target], False
    if o == 'dbcc': return [n, i.target], False
    if o == 'bsr': return [i.target], False
    if o == 'jsr':
        if i.dst.m in ('absl', 'absw'): return [i.dst.v], False
        return [], True
    if o == 'jmp':
        if i.dst.m in ('absl', 'absw'): return [i.dst.v], False
        return [], True
    if o in ('rts', 'rte', 'rtr'): return [], True
    return [n], False

RET_PTS = sorted(NEXT[a] for a in ADDRS if INS[a].op in ('bsr', 'jsr') and NEXT[a] in INS)
IRQ_PTS = sorted(set(INS[a].target for a in ADDRS if INS[a].op in ('bcc', 'bra', 'dbcc') and INS[a].target <= a and INS[a].target in INS))

def func_bodies():
    """rts alcanzables desde cada entrada de funcion (sin entrar en las llamadas)"""
    callers = {}
    for a in ADDRS:
        i = INS[a]
        if i.op == 'bsr' or (i.op == 'jsr' and i.dst.m in ('absl', 'absw')):
            t = i.target if i.op == 'bsr' else i.dst.v
            callers.setdefault(t, []).append(a)
    rts_of = {}
    for f in callers:
        seen = set(); st = [f]
        while st:
            a = st.pop()
            if a in seen or a not in INS: continue
            seen.add(a)
            i = INS[a]; o = i.op
            if o == 'rts': rts_of.setdefault(a, set()).add(f); continue
            if o in ('rte', 'rtr'): continue
            if o == 'jmp' and i.dst.m not in ('absl', 'absw'): continue
            if o in ('bsr', 'jsr'): st.append(NEXT[a]); continue
            ss, ind = succs(i)
            st.extend(ss)
    return callers, rts_of

def liveness():
    """vida de las banderas. rts: union de lo vivo en los puntos de retorno de las llamadas a las funciones
    que lo contienen (TODO si no se sabe); rte: union de los puntos de IRQ; jmp/jsr indirecto: todo"""
    callers, rts_of = func_bodies()
    ind_ret = [NEXT[a] for a in ADDRS if INS[a].op == 'jsr' and INS[a].dst.m not in ('absl', 'absw')]
    live_in = {a: 0 for a in ADDRS}
    info = {a: (uses_defs(INS[a]), succs(INS[a])) for a in ADDRS}
    def rts_out(a):
        if a not in rts_of: return ALL
        out = 0
        for f in rts_of[a]:
            for c in callers[f]: out |= live_in.get(NEXT[c], ALL)
        for r in ind_ret: out |= live_in.get(r, ALL)
        return out
    def outset(a):
        (u, d), (ss, ind) = info[a]
        o = INS[a].op
        if ind: out = rts_out(a) if o == 'rts' else ALL
        else: out = 0
        for s_ in ss: out |= live_in.get(s_, ALL)
        return out
    changed = True
    while changed:
        changed = False
        for a in reversed(ADDRS):
            (u, d), _ = info[a]
            li = u | (outset(a) & ~d)
            if li != live_in[a]:
                live_in[a] = li; changed = True
    live_out = {a: outset(a) for a in ADDRS}
    nall = sum(1 for a in ADDRS if INS[a].op == 'rts' and live_out[a] == ALL)
    print('rts con todo vivo: %d de %d' % (nall, sum(1 for a in ADDRS if INS[a].op == 'rts')), file=sys.stderr)
    return live_in, live_out

# ---------------------------------------------------------------- etiquetas y despacho
def labels():
    direct = set(); taken = set([RESET_PC, IRQ6])
    for v in range(1, 64):
        x = struct.unpack('>I', D[v * 4:v * 4 + 4])[0]
        if x in INS: taken.add(x)
    reach = set()
    for a in ADDRS:
        i = INS[a]
        ss, ind = succs(i)
        for s in ss:
            if s != NEXT[a] or i.target == s: direct.add(s)
            if s not in INS: print('AVISO destino fuera de codigo %06x -> %06x' % (a, s), file=sys.stderr)
        if i.op not in ('bra', 'jmp', 'rts', 'rte', 'rtr'): reach.add(NEXT[a])
        if i.op in ('bsr', 'jsr'): taken.add(NEXT[a]); reach.add(NEXT[a])
        if i.op in ('bcc', 'bra', 'dbcc') and i.target <= a: taken.add(i.target)   # vuelta de IRQ
    for a in ADDRS:
        if a not in reach and a not in direct: taken.add(a)   # solo alcanzable de forma indirecta
    # punteros a codigo en la ROM (tablas de saltos, punteros a funcion copiados a RAM)
    for o in range(0, len(D) - 3, 2):
        v = struct.unpack('>I', D[o:o + 4])[0]
        if v in INS and v >= 0x400: taken.add(v)
    taken = set(t for t in taken if t in INS)
    return direct | taken, taken

# ---------------------------------------------------------------- generacion de C
MASK = {1: '0xFFu', 2: '0xFFFFu', 4: '0xFFFFFFFFu'}
SIGN = {1: '0x80u', 2: '0x8000u', 4: '0x80000000u'}
BITS = {1: 8, 2: 16, 4: 32}
RAM0 = 0xFEC000

def region(a):
    a &= 0xFFFFFF
    if a < 0x100000: return 'rom', a
    if 0xFEC000 <= a < 0xFF0000: return 'ram', a - 0xFEC000
    if 0x200000 <= a < 0x204000: return 'pal', a - 0x200000
    if 0x440000 <= a < 0x441000: return 'spr', a - 0x440000
    return None, a

def rd_const(a, sz):
    reg, o = region(a)
    if reg:
        if sz == 1: return 'MB(%s, 0x%x)' % (reg, o)
        if sz == 2: return 'MW(%s, 0x%x)' % (reg, o)
        return '%s(%s, 0x%x)' % ('ML' if o & 3 else 'MLA', reg, o)
    return 'RD%d(0x%06xu)' % (sz * 8, a)

def wr_const(a, sz, v, pc):
    reg, o = region(a)
    if reg and reg != 'rom':
        if sz == 1: return 'MB(%s, 0x%x) = (u8)(%s);' % (reg, o, v)
        if sz == 2 and a == 0xFEC102: return 'MW(%s, 0x%x) = (u16)(%s); if (((%s) & 0xFFFF) == 1) RC_CALL(rc_mcu());' % (reg, o, v, v)
        if sz == 2 and a == 0xFEF500: return 'MW(%s, 0x%x) = (u16)(%s); RC_CALL(rc_mcu());   /* la MCU contesta al momento */' % (reg, o, v)
        if sz == 2: return 'MW(%s, 0x%x) = (u16)(%s);' % (reg, o, v)
        return 'SETL(%s, 0x%x, %s);' % (reg, o, v)
    return 'WR%d(0x%06xu, %s, 0x%x);' % (sz * 8, a, v, pc)

def idx_expr(x):
    k, n, lng = x
    r = '%s%d' % (k.lower(), n)
    return r if lng else '(u32)(s32)(s16)%s' % r

class G:
    def __init__(s, i, live_out):
        s.i = i; s.lo = live_out; s.out = []
    def e(s, line): s.out.append(line)

    def inc(s, ea, sz): return 2 if (sz == 1 and ea.r == 7) else sz

    def addr(s, ea, sz, tmp):
        """emite el calculo de direccion en tmp; devuelve (expr, const|None, post)"""
        m = ea.m
        if m in ('absw', 'absl', 'pcd'): return None, ea.v, ''
        if m == 'ind': s.e('%s = a%d;' % (tmp, ea.r)); return tmp, None, ''
        if m == 'post':
            s.e('%s = a%d;' % (tmp, ea.r)); return tmp, None, 'a%d += %d;' % (ea.r, s.inc(ea, sz))
        if m == 'pre':
            s.e('a%d -= %d; %s = a%d;' % (ea.r, s.inc(ea, sz), tmp, ea.r)); return tmp, None, ''
        if m == 'd16': s.e('%s = a%d + (u32)(%d);' % (tmp, ea.r, ea.v)); return tmp, None, ''
        if m == 'idx': s.e('%s = a%d + (u32)(%d) + %s;' % (tmp, ea.r, ea.v, idx_expr(ea.x))); return tmp, None, ''
        if m == 'pci': s.e('%s = 0x%xu + %s;' % (tmp, ea.v, idx_expr(ea.x))); return tmp, None, ''
        raise ValueError(m)

    def pred(s, k): return PRED.get(s.i.a, (None, None, False))[k]
    def rd(s, expr, const, sz):
        if const is not None: return rd_const(const, sz)
        if s.pred(0) and sz == 4 and s.pred(2): return 'RDA32(%s, %s)' % (s.pred(0), expr)
        if s.pred(0): return 'RDP%d(%s, %s)' % (sz * 8, s.pred(0), expr)
        return 'RD%d(%s)' % (sz * 8, expr)
    def wr(s, expr, const, sz, v):
        if const is not None: return wr_const(const, sz, v, s.i.a)
        # VRAM: descifrado fuera de linea sin pasar por la tabla de paginas (los bytes van por el camino general)
        if s.pred(1) == 'vram' and sz > 1: return 'WRV%d(%s, %s, 0x%x);' % (sz * 8, expr, v, s.i.a)
        if s.pred(1) and s.pred(1) != 'vram': return 'WRP%d(%s, %s, %s, 0x%x);' % (sz * 8, s.pred(1), expr, v, s.i.a)
        return 'WR%d(%s, %s, 0x%x);' % (sz * 8, expr, v, s.i.a)

    def read(s, ea, sz, tmp='eas'):
        """lee un operando; devuelve expr (ya evaluada en variable si es memoria)"""
        if ea.m == 'D': return '(d%d & %s)' % (ea.r, MASK[sz]) if sz < 4 else 'd%d' % ea.r
        if ea.m == 'A': return '(a%d & %s)' % (ea.r, MASK[sz]) if sz < 4 else 'a%d' % ea.r
        if ea.m == 'imm': return '0x%xu' % (ea.v & int(MASK[sz][:-1], 16))
        expr, const, post = s.addr(ea, sz, tmp)
        var = 'vs' if tmp == 'eas' else 'vd'
        s.e('%s = %s;' % (var, s.rd(expr, const, sz)))
        if post: s.e(post)
        return var

    def setreg(s, ea, sz, v):
        r = '%s%d' % ('d' if ea.m == 'D' else 'a', ea.r)
        if ea.m == 'A' or sz == 4: s.e('%s = %s;' % (r, v))
        # parte baja con xor: 3 instrucciones del MIPS, sin cargar la mascara de la parte alta
        elif sz == 2: s.e('%s ^= (%s ^ (%s)) & 0xFFFFu;' % (r, r, v))
        else: s.e('%s ^= (%s ^ (%s)) & 0xFFu;' % (r, r, v))

    def write(s, ea, sz, v):
        if ea.m in ('D', 'A'): s.setreg(ea, sz, v); return
        expr, const, post = s.addr(ea, sz, 'ead')
        s.e(s.wr(expr, const, sz, v))
        if post: s.e(post)

    def rmw(s, ea, sz):
        """lectura de destino para lectura-modificacion-escritura: devuelve (valor, fn_escritura)"""
        if ea.m in ('D', 'A'):
            return s.read(ea, sz), lambda v: s.setreg(ea, sz, v)
        expr, const, post = s.addr(ea, sz, 'ead')
        s.e('vd = %s;' % s.rd(expr, const, sz))
        def w(v):
            s.e(s.wr(expr, const, sz, v))
            if post: s.e(post)
        return 'vd', w

    # banderas
    def fl(s, which): return s.lo & which
    def nz(s, r, sz):
        if s.fl(N): s.e('fN = (%s >> %d) & 1;' % (r, BITS[sz] - 1))
        if s.fl(Z): s.e('fZ = ((%s) & %s) == 0;' % (r, MASK[sz]))
    def vc0(s):
        if s.fl(V): s.e('fV = 0;')
        if s.fl(C): s.e('fC = 0;')
    def logic(s, r, sz): s.nz(r, sz); s.vc0()

    def add_flags(s, dd, ss, r, sz, x=True, cmp_=False):
        b = BITS[sz]
        if s.fl(V): s.e('fV = (((%s ^ %s) & (%s ^ %s)) >> %d) & 1;' % (ss, r, dd, r, b - 1))
        need_c = s.fl(C) or (x and s.fl(X))
        if need_c:
            if sz == 4: s.e('fC = %s < %s;' % (r, dd))
            else: s.e('fC = (%s >> %d) & 1;' % (r, b))
            if x and s.fl(X): s.e('fX = fC;')
        s.nz(r, sz)
    def sub_flags(s, dd, ss, r, sz, x=True):
        b = BITS[sz]
        if s.fl(V): s.e('fV = (((%s ^ %s) & (%s ^ %s)) >> %d) & 1;' % (ss, dd, r, dd, b - 1))
        if s.fl(C) or (x and s.fl(X)):
            s.e('fC = %s > %s;' % (ss, dd))
            if x and s.fl(X): s.e('fX = fC;')
        s.nz(r, sz)

def callpush(i, nxt, labelset):
    """apila la direccion de vuelta"""
    return 'RC_PUSH(0x%xu, 0x%x);' % (nxt, i.a)

def gen_ins(i, live_out, labelset, taken):
    g = G(i, live_out[i.a])
    o, sz = i.op, i.sz
    e = g.e
    nxt = NEXT[i.a]
    def goto(t):
        if t in labelset: return 'goto L_%06x;' % t
        return 'DISPATCH(0x%xu);' % t
    def br_irq(t):
        # salto hacia atras: punto de comprobacion de interrupcion
        if t <= i.a:
            return 'IRQCHK(0x%xu); %s' % (t, goto(t))
        return goto(t)

    if o == 'move':
        v = g.read(i.src, sz)
        if i.src.m != 'D' and i.src.m != 'A' and i.src.m != 'imm':
            pass
        else:
            e('vs = %s;' % v); v = 'vs'
        g.logic(v, sz)
        g.write(i.dst, sz, v)
    elif o == 'movea':
        v = g.read(i.src, sz)
        if sz == 2: e('a%d = (u32)(s32)(s16)%s;' % (i.dst.r, v))
        else: e('a%d = %s;' % (i.dst.r, v))
    elif o == 'moveq':
        e('d%d = 0x%xu;' % (i.dst.r, i.src.v))
        v = 'd%d' % i.dst.r
        g.logic(v, 4)
    elif o == 'lea':
        expr, const, post = g.addr(i.src, 4, 'eas')
        e('a%d = %s;' % (i.dst.r, expr if const is None else '0x%xu' % const))
    elif o == 'pea':
        expr, const, post = g.addr(i.src, 4, 'eas')
        e('vs = %s;' % (expr if const is None else '0x%xu' % const))
        e('a7 -= 4; WR32(a7, vs, 0x%x);' % i.a)
    elif o == 'clr':
        g.write(i.dst, sz, '0')
        if g.fl(N): e('fN = 0;')
        if g.fl(Z): e('fZ = 1;')
        g.vc0()
    elif o == 'tst':
        v = g.read(i.dst, sz)
        g.logic(v, sz)
    elif o in ('add', 'addi', 'addq', 'sub', 'subi', 'subq'):
        s = g.read(i.src, sz)
        if s != 'vs': e('vs = %s;' % s)
        d, w = g.rmw(i.dst, sz)
        if d != 'vd': e('vd = %s;' % d)
        op = '+' if o.startswith('add') else '-'
        # sin mascara: vd y vs ya vienen recortados, la suma cabe en b+1 bits y la resta solo se mira hasta el bit
        # b-1 (C de la resta sale de vs > vd); quien escribe o mira Z recorta
        e('vr = vd %s vs;' % op)
        if op == '+': g.add_flags('vd', 'vs', 'vr', sz)
        else: g.sub_flags('vd', 'vs', 'vr', sz)
        w('vr')
    elif o in ('addqa', 'subqa', 'adda', 'suba'):
        s = g.read(i.src, sz)
        if sz == 2 and o in ('adda', 'suba'): s = '(u32)(s32)(s16)%s' % s
        e('a%d %s= %s;' % (i.dst.r, '+' if o.startswith('add') else '-', s))
    elif o in ('cmp', 'cmpi', 'cmpa', 'cmpm'):
        if o == 'cmpm':
            s = g.read(i.src, sz); e('vs = %s;' % s) if s != 'vs' else None
            d = g.read(i.dst, sz, 'ead')
        else:
            s = g.read(i.src, sz)
            if s != 'vs': e('vs = %s;' % s)
            d = g.read(i.dst, sz if o != 'cmpa' else 4, 'ead')
        if o == 'cmpa':
            if sz == 2: e('vs = (u32)(s32)(s16)vs;')
            sz = 4
        if d != 'vd': e('vd = %s;' % d)
        e('vr = vd - vs;')
        g.sub_flags('vd', 'vs', 'vr', sz, x=False)
    elif o in ('and', 'andi', 'or', 'ori', 'eor', 'eori'):
        s = g.read(i.src, sz)
        if s != 'vs': e('vs = %s;' % s)
        d, w = g.rmw(i.dst, sz)
        op = {'and': '&', 'or': '|', 'eor': '^'}[o.rstrip('i')]
        e('vr = %s %s vs;' % (d, op))
        g.logic('vr', sz); w('vr')
    elif o == 'not':
        d, w = g.rmw(i.dst, sz)
        e('vr = (~%s) & %s;' % (d, MASK[sz])); g.logic('vr', sz); w('vr')
    elif o == 'neg':
        d, w = g.rmw(i.dst, sz)
        if d != 'vd': e('vd = %s;' % d)
        e('vr = (0u - vd) & %s;' % MASK[sz])
        if g.fl(V): e('fV = ((vd & vr) >> %d) & 1;' % (BITS[sz] - 1))
        if g.fl(C) or g.fl(X):
            e('fC = vd != 0;')
            if g.fl(X): e('fX = fC;')
        g.nz('vr', sz); w('vr')
    elif o in ('negx', 'addx', 'subx'):
        if o == 'negx':
            d, w = g.rmw(i.dst, sz); s_ = d
            if d != 'vs': e('vs = %s;' % d)
            e('vd = 0;')
            opx = '-'
        else:
            s_ = g.read(i.src, sz)
            if s_ != 'vs': e('vs = %s;' % s_)
            d, w = g.rmw(i.dst, sz)
            if d != 'vd': e('vd = %s;' % d)
            opx = '+' if o == 'addx' else '-'
        b = BITS[sz]
        if sz == 4:
            e('{ u64 t = (u64)vd %s (u64)vs %s fX; vr = (u32)t;' % (opx, opx))
            e('  fC = (u32)(t >> 32) & 1; }')
        else:
            e('vr = (vd %s vs %s fX) & 0x%x; fC = (vr >> %d) & 1; vr &= %s;' % (opx, opx, (1 << (b + 1)) - 1, b, MASK[sz]))
        if opx == '+': e('fV = (((vs ^ vr) & (vd ^ vr)) >> %d) & 1;' % (b - 1))
        else: e('fV = (((vs ^ vd) & (vr ^ vd)) >> %d) & 1;' % (b - 1))
        e('fX = fC; fN = (vr >> %d) & 1; if (vr & %s) fZ = 0;' % (b - 1, MASK[sz]))
        w('vr')
    elif o == 'ext':
        r = i.dst.r
        if sz == 2: e('d%d ^= (d%d ^ (u32)(s32)(s8)d%d) & 0xFFFFu;' % (r, r, r)); g.logic('d%d' % r, 2)
        else: e('d%d = (u32)(s32)(s16)d%d;' % (r, r)); g.logic('d%d' % r, 4)
    elif o == 'swap':
        r = i.dst.r
        e('d%d = (d%d >> 16) | (d%d << 16);' % (r, r, r)); g.logic('d%d' % r, 4)
    elif o == 'exg':
        a = '%s%d' % (i.src.m.lower(), i.src.r); b = '%s%d' % (i.dst.m.lower(), i.dst.r)
        e('vs = %s; %s = %s; %s = vs;' % (a, a, b, b))
    elif o in ('mulu', 'muls'):
        s = g.read(i.src, 2)
        r = i.dst.r
        if o == 'mulu': e('d%d = (d%d & 0xFFFFu) * (%s);' % (r, r, s))
        else: e('d%d = (u32)((s32)(s16)d%d * (s32)(s16)(%s));' % (r, r, s))
        g.logic('d%d' % r, 4)
    elif o in ('divu', 'divs'):
        s = g.read(i.src, 2)
        if s != 'vs': e('vs = %s;' % s)
        r = i.dst.r
        e('if (vs == 0) { DIVZERO(0x%x); }' % i.a)
        if o == 'divu':
            e('else { u32 q = d%d / vs, m = d%d %% vs;' % (r, r))
            e('  if (q < 0x10000u) { d%d = q | (m << 16); fZ = q == 0; fN = (q >> 15) & 1; fV = 0; fC = 0; } else fV = 1; }' % r)
        else:
            e('else { s32 dv = (s32)d%d, sv = (s32)(s16)vs;' % r)
            e('  if (dv == (s32)0x80000000 && sv == -1) { fZ = 0; fN = 0; fV = 0; fC = 0; d%d = 0; }' % r)
            e('  else { s32 q = dv / sv, m = dv % sv;')
            e('    if (q == (s32)(s16)q) { d%d = ((u32)q & 0xFFFFu) | ((u32)m << 16); fZ = (q & 0xFFFF) == 0; fN = ((u32)q >> 15) & 1; fV = 0; fC = 0; } else fV = 1; } }' % r)
    elif o in ('btst', 'bchg', 'bclr', 'bset'):
        if i.src.m == 'imm': bit = '%du' % (i.src.v & (31 if i.dst.m == 'D' else 7))
        else: bit = '(d%d & %d)' % (i.src.r, 31 if i.dst.m == 'D' else 7)
        bsz = 4 if i.dst.m == 'D' else 1
        if o == 'btst':
            v = g.read(i.dst, bsz, 'ead')
            e('fZ = ((%s >> %s) & 1) == 0;' % (v, bit))
        else:
            d, w = g.rmw(i.dst, bsz)
            e('fZ = ((%s >> %s) & 1) == 0;' % (d, bit))
            op = {'bchg': '%s ^ (1u << %s)', 'bclr': '%s & ~(1u << %s)', 'bset': '%s | (1u << %s)'}[o] % (d, bit)
            e('vr = %s;' % op); w('vr')
    elif o in ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr'):
        b = BITS[sz]
        d, w = g.rmw(i.dst, sz)
        if d != 'vd': e('vd = %s;' % d)
        if i.src.m == 'imm':
            c = i.src.v
            if o == 'lsl':
                e('vr = (vd << %d) & %s;' % (c, MASK[sz]))
                cf = '(vd >> %d) & 1' % (b - c) if c <= b else '0'
            elif o == 'lsr':
                e('vr = vd >> %d;' % c)
                cf = '(vd >> %d) & 1' % (c - 1)
            elif o == 'asl':
                e('vr = (vd << %d) & %s;' % (c, MASK[sz]))
                cf = '(vd >> %d) & 1' % (b - c)
                if g.fl(V):
                    if c >= b: e('fV = vd != 0;')
                    else:
                        mk = ((1 << (c + 1)) - 1) << (b - c - 1)
                        e('{ u32 t = vd & 0x%xu; fV = t != 0 && t != 0x%xu; }' % (mk, mk))
            elif o == 'asr':
                e('vr = (u32)((%s)(vd) >> %d) & %s;' % ({1: 's32)(s8', 2: 's32)(s16', 4: 's32'}[sz], c, MASK[sz]) if sz < 4 else 'vr = (u32)((s32)vd >> %d);' % c)
                cf = '(vd >> %d) & 1' % (c - 1)
            elif o == 'rol':
                cc = c % b
                if cc: e('vr = ((vd << %d) | (vd >> %d)) & %s;' % (cc, b - cc, MASK[sz]))
                else: e('vr = vd;')
                cf = 'vr & 1'
            elif o == 'ror':
                cc = c % b
                if cc: e('vr = ((vd >> %d) | (vd << %d)) & %s;' % (cc, b - cc, MASK[sz]))
                else: e('vr = vd;')
                cf = '(vr >> %d) & 1' % (b - 1)
            else:
                e('vr = %s(vd, %d, %d, &fX);' % ('roxl_n' if o == 'roxl' else 'roxr_n', c, b))
                cf = 'fX'
            if o.startswith('rox'):
                e('fC = fX;')
            else:
                if g.fl(C) or (g.fl(X) and o[:2] in ('as', 'ls')):
                    e('fC = %s;' % cf)
                    if o[:2] in ('as', 'ls') and g.fl(X): e('fX = fC;')
            if o != 'asl' and g.fl(V): e('fV = 0;')
            g.nz('vr', sz)
        else:
            e('{ u32 tx = fX, tc, tv; vr = sh_%s(vd, d%d & 63, %d, &tx, &tc, &tv); fX = tx; fC = tc; fV = tv; }' % (o, i.src.r, b))
            g.nz('vr', sz)
        w('vr')
    elif o == 'scc':
        g.write(i.dst, 1, '(%s ? 0xFF : 0)' % COND_EXPR[i.cond])
    elif o == 'dbcc':
        r = i.dst.r
        pre = '' if i.cond == 'f' else 'if (!%s) ' % COND_EXPR[i.cond]
        # parte baja distinta de 0: restar 1 no toca la alta; si es 0 pasa a 0xFFFF y se sale del bucle
        e('%s{ if (d%d & 0xFFFFu) { d%d -= 1; CYCB(10); %s } d%d |= 0xFFFFu; }' % (pre, r, r, br_irq(i.target), r))
    elif o == 'bcc':
        e('if %s { %s }' % (COND_EXPR[i.cond], br_irq(i.target)))
    elif o == 'bra':
        e(br_irq(i.target))
    elif o == 'bsr':
        e('%s %s' % (callpush(i, nxt, labelset), goto(i.target)))
    elif o in ('jsr', 'jmp'):
        if i.dst.m in ('absl', 'absw'): t = '0x%xu' % i.dst.v; tc = i.dst.v
        else:
            expr, const, post = g.addr(i.dst, 4, 'eas'); t = expr; tc = None
        if o == 'jsr': e(callpush(i, nxt, labelset))
        if tc is not None: e(goto(tc))
        else: e('DISPATCH(%s);' % t)
    elif o == 'rts':
        e('RTS();')
    elif o == 'rte':
        e('vs = RDP16(ram, a7); SETSR(vs); ead = a7 + 2; vd = RDP32(ram, ead); a7 += 6; DISPATCH(vd);')
    elif o == 'nop':
        pass
    elif o == 'movem':
        # con region prevista: si el bloque entero cabe en ella, accesos directos; si no, uno a uno por el
        # camino general (en el mismo orden que el 68000)
        mask = i.extra
        regs = [('d%d' % k) for k in range(8)] + [('a%d' % k) for k in range(8)]
        nb = bin(mask).count('1') * sz
        def blk(pr, ea):
            e('if (RBLK(%s, %s, %d)) { u8 *_q = RPTR(%s, %s);' % (pr, ea, nb, pr, ea))
        if i.n == 0:   # registros -> memoria
            pw = g.pred(1) if g.pred(1) != 'vram' else None
            if i.dst.m == 'pre':
                ar = i.dst.r
                order = [regs[15 - bit] for bit in range(16) if mask & (1 << bit)]
                if pw:
                    e('ead = a%d - %d;' % (ar, nb)); blk(pw, 'ead')
                    for k, rg in enumerate(order):
                        off = nb - (k + 1) * sz
                        e('    ' + ('SETL(_q, %d, %s);' % (off, rg) if sz == 4 else 'MW(_q, %d) = (u16)%s;' % (off, rg)))
                    e('} else { ead = a%d;' % ar)
                    for rg in order: e('    ead -= %d; rc_wrg%d(ead, %s, 0x%x);' % (sz, sz * 8, rg, i.a))
                    e('}')
                else:
                    e('ead = a%d;' % ar)
                    for rg in order: e('ead -= %d; WR%d(ead, %s, 0x%x);' % (sz, sz * 8, rg, i.a))
                e('a%d = ead;' % ar)
            else:
                expr, const, post = g.addr(i.dst, sz, 'ead')
                if const is not None: e('ead = 0x%xu;' % const)
                order = [regs[bit] for bit in range(16) if mask & (1 << bit)]
                if pw:
                    blk(pw, 'ead')
                    for k, rg in enumerate(order):
                        e('    ' + ('SETL(_q, %d, %s);' % (k * sz, rg) if sz == 4 else 'MW(_q, %d) = (u16)%s;' % (k * sz, rg)))
                    e('} else {')
                    for k, rg in enumerate(order): e('    rc_wrg%d(ead + %d, %s, 0x%x);' % (sz * 8, k * sz, rg, i.a))
                    e('}')
                else:
                    for rg in order: e('WR%d(ead, %s, 0x%x); ead += %d;' % (sz * 8, rg, i.a, sz))
        else:
            m = i.src.m
            pr = g.pred(0)
            if m == 'post':
                e('eas = a%d;' % i.src.r)
            else:
                expr, const, post = g.addr(i.src, sz, 'eas')
                if const is not None: e('eas = 0x%xu;' % const)
            order = [regs[bit] for bit in range(16) if mask & (1 << bit)]
            if pr:
                blk(pr, 'eas')
                if sz == 4 and len(order) > 1:
                    # alineado a 4 (lo normal salvo en la pila dentro de la IRQ): una lectura de 32 bits por registro
                    e('    if (!(eas & 2)) {')
                    for k, rg in enumerate(order): e('        %s = MLA(_q, %d);' % (rg, k * 4))
                    e('    } else {')
                    for k, rg in enumerate(order): e('        %s = ML(_q, %d);' % (rg, k * 4))
                    e('    }')
                else:
                    for k, rg in enumerate(order):
                        e('    ' + ('%s = ML(_q, %d);' % (rg, k * sz) if sz == 4 else '%s = (u32)(s32)(s16)MW(_q, %d);' % (rg, k * sz)))
                e('} else {')
                for k, rg in enumerate(order):
                    e('    ' + ('%s = rc_rdg32(eas + %d);' % (rg, k * sz) if sz == 4 else '%s = (u32)(s32)(s16)rc_rdg16(eas + %d);' % (rg, k * sz)))
                e('}')
                if m == 'post': e('a%d = eas + %d;' % (i.src.r, nb))
            else:
                for rg in order:
                    if sz == 2: e('%s = (u32)(s32)(s16)RD16(eas); eas += 2;' % rg)
                    else: e('%s = RD32(eas); eas += 4;' % rg)
                if m == 'post': e('a%d = eas;' % i.src.r)
    elif o == 'move_from_sr':
        g.write(i.dst, 2, 'GETSR()')
    elif o == 'move_to_sr':
        v = g.read(i.src, 2); e('SETSR(%s);' % v)
    elif o == 'move_to_ccr':
        v = g.read(i.src, 2); e('SETCCR(%s);' % v)
    elif o in ('ori_sr', 'andi_sr', 'eori_sr'):
        op = {'ori_sr': '|', 'andi_sr': '&', 'eori_sr': '^'}[o]
        e('SETSR(GETSR() %s 0x%xu);' % (op, i.src.v))
    else:
        raise ValueError('sin implementar: %r' % i)
    # transferencia de control al final
    if o in ('bra', 'jmp', 'rts', 'rte', 'jsr', 'bsr'):
        pass
    elif nxt not in INS:
        g.e('FELL(0x%x);' % nxt)
    return g.out

def cycles(i):
    c = 4 + 4 * (i.size // 2 - 1)
    for ea in (i.src, i.dst):
        if ea is not None and ea.mem():
            c += 4 if i.sz != 4 else 8
            if ea.m in ('idx', 'pci', 'pre'): c += 2
    o = i.op
    if o in ('mulu', 'muls'): c += 60
    if o in ('divu', 'divs'): c += 130
    if o in ('rts', 'jsr', 'bsr'): c += 12
    if o == 'rte': c += 16
    if o == 'movem': c += 4 * bin(i.extra).count('1') * (2 if i.sz == 4 else 1)
    if o in ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr') and i.src.m == 'imm': c += 2 + 2 * i.src.v
    return c

# bucles de espera de fin de cuadro: tst.w $fec1a6 / beq al tst (o tst.x mem / bxx atras a si mismo)
IDLE = set()
for _a in ADDRS:
    _i = INS[_a]
    if _i.op == 'tst' and _i.dst.m in ('absl', 'absw') and NEXT[_a] in INS:
        _j = INS[NEXT[_a]]
        if _j.op == 'bcc' and _j.target == _a: IDLE.add(NEXT[_a])

HN, HS = 4096, 17
# la clave del par de escrituras a la VRAM (RC_VLK en rc_rt.h) guarda 18 bits del pc
assert ADDRS[-1] < 0x40000, 'codigo 68k por encima de 0x40000: ampliar RC_VLK en rc_rt.h'
def main(out):
    live_in, live_out = liveness()
    labs, taken = labels()
    f = open(out, 'w')
    w = f.write
    w('/* generado por rc68.py: World Rally 68000 -> C (uso personal) */\n#define RC_GEN 1\n#define RC_HN %d\n#define RC_HS %d\n' % (HN, HS))
    w('#include "rc_rt.h"\n#define RC_IRQ6 0x%xu\n' % IRQ6)
    w('const u32 rc_ntaken = %d;\n' % len(taken))
    w('void rc_run(u32 start_pc)\n{\n    RC_LOCALS\n')
    # despacho (rts, rte, jmp indirecto): tabla hash por el pc con las palabras cambiadas (como queda en la RAM,
    # rts lo usa tal cual), sondeo lineal hasta una entrada vacia (clave 0: el pc 0 no es codigo). Con 4096
    # entradas y la clave (k ^ k >> 17) sale a 1,2 lecturas de media
    def sw(pc): return ((pc << 16) | (pc >> 16)) & 0xFFFFFFFF
    slots = {}
    for t in sorted(taken):
        h = (sw(t) ^ (sw(t) >> HS)) & (HN - 1)
        while h in slots: h += 1
        slots[h] = t
    # al menos RC_HN + 1 entradas y la ultima vacia: el sondeo de una clave que no esta (despacho erroneo) acaba
    # dentro de la tabla. Si ht[0] queda libre lleva un centinela: rts con vuelta a 0 no salta a NULL sino a rc_bad
    w('    static const rc_lab ht[%d] = {\n' % (max(max(slots), HN - 1) + 2))
    for h in sorted(slots): w('        [%d] = {0x%xu, &&L_%06x},\n' % (h, sw(slots[h]), slots[h]))
    if 0 not in slots: w('        [0] = {0, &&rc_k0},\n')
    w('    };\n')
    w('    RC_ENTER\n    DISPATCH(start_pc);\n')
    # ciclos sumados por bloque basico (al principio del bloque)
    CF = ('bcc', 'dbcc', 'bra', 'bsr', 'jsr', 'jmp', 'rts', 'rte')
    lead = set(labs)
    for a in ADDRS:
        if INS[a].op in CF: lead.add(NEXT[a])
    bcyc = {}; cur = None; cur_last = None
    for a in ADDRS:
        if a in lead or cur is None or NEXT.get(cur_last, -1) != a: cur = a; bcyc[cur] = 0
        bcyc[cur] += cycles(INS[a]); cur_last = a
    for a in ADDRS:
        i = INS[a]
        lines = gen_ins(i, live_out, labs, taken)
        lab = 'L_%06x: ' % a if a in labs else ''
        cy = ' CYC(%d);' % bcyc[a] if a in bcyc else ''
        w('%sCK(0x%x, %d);%s' % (lab, a, live_in[a], cy))
        if a in IDLE: w(' IDLEHINT(%s);' % COND_EXPR[i.cond])
        w(' /* %r */\n' % i)
        for l in lines: w('    ' + l + '\n')
    w('rc_k0:\n    dpc = 0; goto bad_dispatch;\n')
    w('    RC_EXIT_CODE\n}\n')
    f.close()
    print('instr', len(ADDRS), 'etiquetas', len(labs), 'despacho', len(taken), 'esperas', ' '.join('%x' % x for x in sorted(IDLE)), file=sys.stderr)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'wr_rc.c')
