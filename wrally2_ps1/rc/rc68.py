import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work, WR1
sys.path.insert(0, os.path.join(WR1, 'rc'))
"""Recompilador estatico 68000 -> C para World Rally 2 (port PS1, uso personal). Copia del de World Rally 1
(wrally_ps1/rc/rc68.py) con el mapa de memoria del WR2: RAM de 64 KB en 0xFE0000, VRAM de 64 KB sin cifrar en
0x200000 con los registros del sonido (GAE1) en 0x202890-0x2028FF, paleta en 0x210000, registros de video en 0x218000;
la MCU contesta al escribir 1 en 0xFEFC14/0xFEFC18; espera de cada cuadro con contador (ESPERAS).
Genera una unica funcion C con una etiqueta por instruccion (goto para saltos directos, tabla de
despacho para rts/rte/jmp indirectos), banderas solo donde se usan (analisis de vida) y memoria en
palabras de 16 bits en orden nativo (byte a en a^1)."""
import pickle, struct, sys
from dec68 import Dec, EA, Ins

D = open(work('prog.bin'), 'rb').read()
code = {int(l, 16): None for l in open(work('code_addrs.txt')) if l.strip()}   # tools/descent.py
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

# veces que se ejecuta cada instruccion en el banco de pruebas, en total y como mucho en un cuadro (work/vueltas.txt,
# de rc/regiones.py): con ellas se eligen los bucles rapidos (sin el fichero no hay)
VUELTAS, NCUADROS = {}, 0
_vf = work('vueltas.txt')
if os.path.exists(_vf):
    for l in open(_vf):
        p = l.split()
        if l.startswith('# cuadros'): NCUADROS = int(p[2])
        elif len(p) == 4 and not l.startswith('#'): VUELTAS[int(p[0], 16)] = tuple(int(x) for x in p[1:])

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
        if i.op in ('move_to_sr', 'andi_sr', 'eori_sr', 'ori_sr'): taken.add(NEXT[a])   # IRQ al bajar la mascara
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
# registros del GAE1 dentro de la VRAM: las escrituras van al sonido (camino general); las lecturas, a la VRAM, donde
# rc_mem.c deja el estado de los registros
SND_LO, SND_HI = 0x202890, 0x202900

def region(a, w=False):
    a &= 0xFFFFFF
    if a < 0x100000: return 'rom', a
    if 0xFE0000 <= a < 0xFF0000: return 'ram', a - 0xFE0000
    if 0x200000 <= a < 0x210000 and not (w and SND_LO - 2 < a < SND_HI): return 'vram', a - 0x200000
    if 0x210000 <= a < 0x212000: return 'pal', a - 0x210000
    if 0x218000 <= a < 0x219000: return 'vregs', a - 0x218000
    return None, a

def rd_const(a, sz):
    reg, o = region(a)
    if reg:
        if sz == 1: return 'MB(%s, 0x%x)' % (reg, o)
        if sz == 2: return 'MW(%s, 0x%x)' % (reg, o)
        return '%s(%s, 0x%x)' % ('ML' if o & 3 else 'MLA', reg, o)
    return 'RD%d(0x%06xu)' % (sz * 8, a)

# peticiones de trabajo a la MCU (move.w #1 en 0x3684 y 0x37C8): se hace al momento (determinista, como el WR1)
MCU_PET = {0xFEFC14: 'rc_mcu_a', 0xFEFC18: 'rc_mcu_b'}
# eco complementado (not.w $fefc08 / $fefc0c): la MCU lo contesta en menos de 150 us y el juego lo comprueba en el
# mismo cuadro (pantalla de prueba del arranque): tambien al momento
MCU_ECO = (0xFEFC08, 0xFEFC0C)

def wr_const(a, sz, v, pc):
    reg, o = region(a, True)
    if reg and reg != 'rom':
        if sz == 1: return 'MB(%s, 0x%x) = (u8)(%s);' % (reg, o, v)
        if sz == 2 and a in MCU_PET: return 'MW(%s, 0x%x) = (u16)(%s); if (((%s) & 0xFF) == 1) RC_CALL(%s());' % (reg, o, v, v, MCU_PET[a])
        if sz == 2 and a in MCU_ECO: return 'MW(%s, 0x%x) = (u16)(%s); RC_CALL(rc_mcu_eco());' % (reg, o, v)
        if sz == 2: return 'MW(%s, 0x%x) = (u16)(%s);' % (reg, o, v)
        return 'SETL(%s, 0x%x, %s);' % (reg, o, v)
    return 'WR%d(0x%06xu, %s, 0x%x);' % (sz * 8, a, v, pc)

def idx_expr(x):
    k, n, lng = x
    r = '%s%d' % (k.lower(), n)
    return r if lng else '(u32)(s32)(s16)%s' % r

class G:
    def __init__(s, i, live_out, cx=None):
        s.i = i; s.lo = live_out; s.out = []
        # copia rapida de un bucle (bucles_rapidos): accesos sin comprobar y Dn partidos (solo la parte baja)
        L = cx.fast if cx else None
        s.split = L.split if L else ()
        s.unchk = L.unchk.get(i.a, {}) if L else {}
        s.dirty = False     # el valor a escribir puede pasar de 16 bits (suma/resta sin recortar)
    def e(s, line): s.out.append(line)
    def role(s, ea, k): return s.unchk.get(('src' if ea is s.i.src else 'dst', k)) if ea is not None else None

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
    def rd(s, expr, const, sz, ea=None):
        if const is not None: return rd_const(const, sz)
        u = s.role(ea, 'r')
        if u: return 'RDU%d%s(%s, %s)' % (sz * 8, 'A' if u[1] and sz == 4 else '', u[0], expr)
        if s.pred(0) and sz == 4 and s.pred(2): return 'RDA32(%s, %s)' % (s.pred(0), expr)
        if s.pred(0): return 'RDP%d(%s, %s)' % (sz * 8, s.pred(0), expr)
        return 'RD%d(%s)' % (sz * 8, expr)
    def wr(s, expr, const, sz, v, ea=None):
        if const is not None: return wr_const(const, sz, v, s.i.a)
        if s.role(ea, 'w'): return 'WRU%d(%s, %s, %s);' % (sz * 8, s.role(ea, 'w')[0], expr, v)
        if s.pred(1): return 'WRP%d(%s, %s, %s, 0x%x);' % (sz * 8, s.pred(1), expr, v, s.i.a)
        return 'WR%d(%s, %s, 0x%x);' % (sz * 8, expr, v, s.i.a)

    def read(s, ea, sz, tmp='eas'):
        """lee un operando; devuelve expr (ya evaluada en variable si es memoria)"""
        if ea.m == 'D' and ea.r in s.split: return 'd%d' % ea.r if sz == 2 else '(d%d & 0xFFu)' % ea.r
        if ea.m == 'D': return '(d%d & %s)' % (ea.r, MASK[sz]) if sz < 4 else 'd%d' % ea.r
        if ea.m == 'A': return '(a%d & %s)' % (ea.r, MASK[sz]) if sz < 4 else 'a%d' % ea.r
        if ea.m == 'imm': return '0x%xu' % (ea.v & int(MASK[sz][:-1], 16))
        expr, const, post = s.addr(ea, sz, tmp)
        var = 'vs' if tmp == 'eas' else 'vd'
        s.e('%s = %s;' % (var, s.rd(expr, const, sz, ea)))
        if post: s.e(post)
        return var

    def setreg(s, ea, sz, v):
        r = '%s%d' % ('d' if ea.m == 'D' else 'a', ea.r)
        if ea.m == 'A' or sz == 4: s.e('%s = %s;' % (r, v))
        # Dn partido: solo la parte baja, con la alta a 0 (lo leido en .w ya viene recortado)
        elif ea.r in s.split and sz == 2: s.e(('%s = (%s) & 0xFFFFu;' if s.dirty else '%s = %s;') % (r, v))
        # parte baja con xor: 3 instrucciones del MIPS, sin cargar la mascara de la parte alta
        elif sz == 2: s.e('%s ^= (%s ^ (%s)) & 0xFFFFu;' % (r, r, v))
        else: s.e('%s ^= (%s ^ (%s)) & 0xFFu;' % (r, r, v))

    def write(s, ea, sz, v):
        if ea.m in ('D', 'A'): s.setreg(ea, sz, v); return
        expr, const, post = s.addr(ea, sz, 'ead')
        s.e(s.wr(expr, const, sz, v, ea))
        if post: s.e(post)

    def rmw(s, ea, sz):
        """lectura de destino para lectura-modificacion-escritura: devuelve (valor, fn_escritura)"""
        if ea.m in ('D', 'A'):
            return s.read(ea, sz), lambda v: s.setreg(ea, sz, v)
        expr, const, post = s.addr(ea, sz, 'ead')
        s.e('vd = %s;' % s.rd(expr, const, sz, ea))
        def w(v):
            s.e(s.wr(expr, const, sz, v, ea))
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

# comparacion, resta u operacion logica seguida de bcc que mira N o V: la condicion sale de los operandos (vd - vs,
# recortados a sz) sin volver a leer fN/fV (variables en la pila). Las banderas se calculan igual que sin esto: las
# muertas tambien acaban en el SR que se apila en la IRQ (RAM del juego)
SCAST = {1: '(s32)(s8)', 2: '(s32)(s16)', 4: '(s32)'}
FZ_SUB = ('cmp', 'cmpi', 'cmpa', 'cmpm', 'sub', 'subi', 'subq')
FZ_LOG = ('move', 'tst', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'not')
def fz_sub(c, sz):
    S, sg = SCAST[sz], SIGN[sz]
    v = '(((vs ^ vd) & (vr ^ vd)) & %s)' % sg
    return {'eq': '(vd == vs)', 'ne': '(vd != vs)', 'cs': '(vs > vd)', 'cc': '(vs <= vd)', 'hi': '(vd > vs)',
            'ls': '(vd <= vs)', 'lt': '(%svd < %svs)' % (S, S), 'ge': '(%svd >= %svs)' % (S, S),
            'gt': '(%svd > %svs)' % (S, S), 'le': '(%svd <= %svs)' % (S, S), 'mi': '((vr & %s) != 0)' % sg,
            'pl': '((vr & %s) == 0)' % sg, 'vs': '(%s != 0)' % v, 'vc': '(%s == 0)' % v}[c]
def fz_log(c, x, sz):
    # tras una operacion logica (V = C = 0) con resultado x
    S, m, sg = SCAST[sz], MASK[sz], SIGN[sz]
    z, nz = '(((%s) & %s) == 0)' % (x, m), '(((%s) & %s) != 0)' % (x, m)
    n, p = '(((%s) & %s) != 0)' % (x, sg), '(((%s) & %s) == 0)' % (x, sg)
    return {'eq': z, 'ls': z, 'ne': nz, 'hi': nz, 'mi': n, 'lt': n, 'pl': p, 'ge': p, 'gt': '(%s(%s) > 0)' % (S, x),
            'le': '(%s(%s) <= 0)' % (S, x), 'cs': '(0)', 'vs': '(0)', 'cc': '(1)', 'vc': '(1)'}[c]

class Ctx:
    # estado de una pasada de generacion: etiquetas, bcc fusionados (direccion -> condicion), copia rapida de
    # bucle en curso (fast) y saltos atras del codigo normal que van a la etiqueta sin la entrada rapida (slow)
    def __init__(s, labs, live_out):
        s.labs = labs; s.lo = live_out; s.fuse = {}; s.fast = None; s.slow = {}
        s.defer = []; s.dflags = 0; s.live_in = None

def fusable(i, cx):
    if i.op not in FZ_SUB and i.op not in FZ_LOG: return None
    n = NEXT[i.a]
    if n not in INS or n in cx.labs or n in IDLE or n in ESPERAS: return None
    j = INS[n]
    L = cx.fast
    if L and n == L.b and L.defer_ok: return j if j.op == 'bcc' and j.cond not in ('t', 'f') else None
    if j.op != 'bcc' or j.cond not in ('lt', 'ge', 'gt', 'le', 'mi', 'pl', 'vs', 'vc'): return None
    return j

def gen_ins(i, live_out, labelset, taken, cx=None):
    cx = cx or Ctx(labelset, live_out)
    L = cx.fast
    g = G(i, live_out[i.a], cx)
    o, sz = i.op, i.sz
    e = g.e
    nxt = NEXT[i.a]
    fj = fusable(i, cx)
    # copia rapida sin otras salidas: las banderas de la instruccion que decide la vuelta solo se escriben al salir
    # (con los mismos operandos), salvo las que lee el propio cuerpo
    dfr = fj is not None and L is not None and fj.a == L.b and L.defer_ok
    if dfr: g.lo = live_out[i.a] & cx.live_in[L.h]
    def defer(fn):
        if not dfr: return
        g2 = G(i, live_out[i.a]); fn(g2)
        cx.defer = g2.out; cx.dflags = uses_defs(i)[1] & live_out[i.a] & ~cx.live_in[L.h]
    def jump(t):
        if L and t in L.bodyset and t > i.a: return 'goto F_%06x;' % t
        if not L and cx.slow.get(i.a) == t: return 'goto S_%06x;' % t
        if t in labelset: return 'goto L_%06x;' % t
        return 'DISPATCH(0x%xu);' % t
    def goto(t):
        # en la copia rapida, lo que sale del cuerpo deja antes los registros y el contador como en el codigo normal
        if L and not (t in L.bodyset and t > i.a): return L.rest(i.a) + jump(t)
        return jump(t)
    def br_irq(t):
        # salto hacia atras: punto de comprobacion de interrupcion
        if t <= i.a:
            return (L.rest(i.a) if L else '') + 'REGBK(0x%x); IRQCHK(0x%xu); %s' % (i.a, t, jump(t))
        return goto(t)
    if L and i.a == L.b:
        # vuelta de la copia rapida: sin IRQ pendiente ni fin de las vueltas previstas (cyr < 0) sigue en ella
        dl = ' '.join(cx.defer) + ' ' if cx.defer else ''
        back = 'REGBK(0x%x); if (cyr < 0) goto F_%06x; %s%sIRQCHK(0x%xu); goto L_%06x;' % (i.a, L.h, dl, L.rest(i.a), L.h, L.h)
        if o == 'bra': e(back)
        elif o == 'bcc': e('if %s { %s }' % (cx.fuse.get(i.a) or COND_EXPR[i.cond], back))
        else:
            r = i.dst.r
            pre = '' if i.cond == 'f' else 'if (!%s) ' % COND_EXPR[i.cond]
            if r in L.split: e('%s{ if (d%d) { d%d -= 1; CYCB(%d); %s } d%d = 0xFFFFu; }' % (pre, r, r, DBCC_TOMADO, back, r))
            else: e('%s{ if (d%d & 0xFFFFu) { d%d -= 1; CYCB(%d); %s } d%d |= 0xFFFFu; }' % (pre, r, r, DBCC_TOMADO, back, r))
        if o != 'bra': e(dl + L.rest(i.a) + jump(nxt))
        return g.out

    if o == 'move':
        v = g.read(i.src, sz)
        if i.src.m != 'D' and i.src.m != 'A' and i.src.m != 'imm':
            pass
        else:
            e('vs = %s;' % v); v = 'vs'
        g.logic(v, sz)
        if fj: cx.fuse[fj.a] = fz_log(fj.cond, v, sz)
        defer(lambda g2: g2.logic(v, sz))
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
        if fj: cx.fuse[fj.a] = fz_log(fj.cond, v, sz)
        defer(lambda g2: g2.logic(v, sz))
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
        if fj: cx.fuse[fj.a] = fz_sub(fj.cond, sz)
        defer(lambda g2: g2.sub_flags('vd', 'vs', 'vr', sz))
        g.dirty = True
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
        if fj: cx.fuse[fj.a] = fz_sub(fj.cond, sz)
        defer(lambda g2: g2.sub_flags('vd', 'vs', 'vr', sz, x=False))
    elif o in ('and', 'andi', 'or', 'ori', 'eor', 'eori'):
        s = g.read(i.src, sz)
        if s != 'vs': e('vs = %s;' % s)
        d, w = g.rmw(i.dst, sz)
        op = {'and': '&', 'or': '|', 'eor': '^'}[o.rstrip('i')]
        e('vr = %s %s vs;' % (d, op))
        g.logic('vr', sz); w('vr')
        if fj: cx.fuse[fj.a] = fz_log(fj.cond, 'vr', sz)
        defer(lambda g2: g2.logic('vr', sz))
    elif o == 'not':
        d, w = g.rmw(i.dst, sz)
        e('vr = (~%s) & %s;' % (d, MASK[sz])); g.logic('vr', sz); w('vr')
        if fj: cx.fuse[fj.a] = fz_log(fj.cond, 'vr', sz)
        defer(lambda g2: g2.logic('vr', sz))
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
        e('%s{ if (d%d & 0xFFFFu) { d%d -= 1; CYCB(%d); %s } d%d |= 0xFFFFu; }' % (pre, r, r, DBCC_TOMADO, br_irq(i.target), r))
    elif o == 'bcc':
        e('if %s { %s }' % (cx.fuse.get(i.a) or COND_EXPR[i.cond], br_irq(i.target)))
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
        e('vs = RDP16(ram, a7); SETSR(vs); ead = a7 + 2; vd = RDP32(ram, ead); a7 += 6; RC_PLAZO(vd); DISPATCH(vd);')
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
            pw = g.pred(1)
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
        # al bajar la mascara, la IRQ pendiente se atiende ya (como el 68000: el arranque del WR2 lo necesita para
        # contar los vblank igual que MAME)
        v = g.read(i.src, 2); e('SETSR(%s); RC_PLAZO(0x%xu);' % (v, nxt))
    elif o == 'move_to_ccr':
        v = g.read(i.src, 2); e('SETCCR(%s);' % v)
    elif o in ('ori_sr', 'andi_sr', 'eori_sr'):
        op = {'ori_sr': '|', 'andi_sr': '&', 'eori_sr': '^'}[o]
        e('SETSR(GETSR() %s 0x%xu); RC_PLAZO(0x%xu);' % (op, i.src.v, nxt))
    else:
        raise ValueError('sin implementar: %r' % i)
    # transferencia de control al final
    if o in ('bra', 'jmp', 'rts', 'rte', 'jsr', 'bsr'):
        pass
    elif nxt not in INS:
        g.e('FELL(0x%x);' % nxt)
    return g.out

# tiempos del 68000 (manual de Motorola, en ciclos de reloj; el WR1 usa una estimacion mas basta). Con ellos el reloj
# del juego avanza como en la placa (13 MHz, 219 966 ciclos por cuadro): p. ej. la suma de la ROM del arranque (0x260C,
# 32 ciclos por vuelta) tarda los mismos cuadros que en MAME. Direccion efectiva: (.b/.w, .l)
EA_T = {'D': (0, 0), 'A': (0, 0), 'ind': (4, 8), 'post': (4, 8), 'pre': (6, 10), 'd16': (8, 12), 'idx': (10, 14),
        'absw': (8, 12), 'absl': (12, 16), 'pcd': (8, 12), 'pci': (10, 14), 'imm': (4, 8)}
LEA_T = {'ind': 4, 'd16': 8, 'idx': 12, 'absw': 8, 'absl': 12, 'pcd': 8, 'pci': 12}
JMP_T = {'ind': 8, 'd16': 10, 'idx': 14, 'absw': 10, 'absl': 12, 'pcd': 10, 'pci': 14}
MOVEM_T = {'ind': 0, 'post': 0, 'pre': 0, 'd16': 4, 'idx': 6, 'absw': 4, 'absl': 8, 'pcd': 4, 'pci': 6}
DBCC_BASE, DBCC_TOMADO = 4, 6     # dbcc: 4 en el bloque y 6 mas si vuelve (10); al acabar la cuenta, 4 (el 68000, 14)

def eat(ea, sz): return EA_T[ea.m][sz == 4] if ea is not None else 0

def cycles(i):
    o, sz, s, d = i.op, i.sz, i.src, i.dst
    L = sz == 4
    rd = lambda ea: ea.m in ('D', 'A')
    if o in ('move', 'movea'):
        return 4 + eat(s, sz) + (EA_T['ind' if d.m == 'pre' else d.m][L] if o == 'move' and d.mem() else 0)
    if o in ('moveq', 'swap', 'ext', 'nop'): return 4
    if o == 'exg': return 6
    if o == 'lea': return LEA_T[s.m]
    if o == 'jmp': return JMP_T[d.m]
    if o == 'jsr': return JMP_T[d.m] + 8
    if o == 'bsr': return 18
    if o in ('bra', 'bcc'): return 10
    if o == 'dbcc': return DBCC_BASE
    if o == 'rts': return 16
    if o in ('rte', 'rtr'): return 20
    if o in ('add', 'sub', 'and', 'or', 'cmp', 'eor', 'addx', 'subx'):
        if d.m == 'D' and o != 'eor': return (6 + (2 if rd(s) or s.m == 'imm' else 0) if L else 4) + eat(s, sz)
        if d.m == 'D': return 8 if L else 4
        return (12 if L else 8) + eat(d, sz)
    if o in ('addi', 'subi', 'andi', 'ori', 'eori'): return (16 if L else 8) if d.m == 'D' else (20 if L else 12) + eat(d, sz)
    if o == 'cmpi': return (14 if L else 8) if d.m == 'D' else (12 if L else 8) + eat(d, sz)
    if o in ('addq', 'subq'): return (8 if L else 4) if d.m == 'D' else 8 if d.m == 'A' else (12 if L else 8) + eat(d, sz)
    if o in ('addqa', 'subqa'): return 8
    if o in ('adda', 'suba'): return (6 + (2 if rd(s) or s.m == 'imm' else 0) if L else 8) + eat(s, sz)
    if o == 'cmpa': return 6 + eat(s, sz)
    if o in ('clr', 'neg', 'not', 'negx'): return (6 if L else 4) if d.m == 'D' else (12 if L else 8) + eat(d, sz)
    if o == 'tst': return 4 + eat(d, sz)
    if o == 'scc': return 6 if d.m == 'D' else 8 + eat(d, 1)
    if o in ('btst', 'bset', 'bclr', 'bchg'):
        im = s.m == 'imm'
        if d.m == 'D': return {'btst': 6, 'bclr': 10}.get(o, 8) + (4 if im else 0)
        return (4 if o == 'btst' else 8) + (4 if im else 0) + eat(d, 1)
    if o in ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr'):
        if d is not None and d.mem(): return 8 + eat(d, 2)
        return (8 if L else 6) + 2 * (s.v if s is not None and s.m == 'imm' else 4)
    if o in ('mulu', 'muls'): return 54 + eat(s, 2)
    if o == 'divu': return 130 + eat(s, 2)
    if o == 'divs': return 150 + eat(s, 2)
    if o == 'movem':
        ea = s if i.n else d
        return (12 if i.n else 8) + MOVEM_T[ea.m] + bin(i.extra).count('1') * (8 if L else 4)
    if o == 'move_to_sr': return 12 + eat(s, 2)
    if o == 'move_to_ccr': return 12 + eat(s, 2)
    if o == 'move_from_sr': return 6 if d.m == 'D' else 8 + eat(d, 2)
    if o in ('ori_sr', 'andi_sr', 'eori_sr', 'ori_ccr', 'andi_ccr', 'eori_ccr'): return 20
    raise ValueError('sin tiempo: %r' % i)

# bucles de espera de fin de cuadro: tst.x mem / bxx atras a si mismo (en el WR2, 0x3362: el primer vblank). Fuera
# las esperas a la MCU (tst.w $fefc12/16 en 0x368C/0x37D0: con el trabajo hecho al momento no esperan nunca, y si se
# difiriera no deben saltar al vblank)
NO_ESPERA = {0x3692, 0x37D6}
IDLE = set()
for _a in ADDRS:
    _i = INS[_a]
    if _i.op == 'tst' and _i.dst.m in ('absl', 'absw') and NEXT[_a] in INS:
        _j = INS[NEXT[_a]]
        if _j.op == 'bcc' and _j.target == _a and NEXT[_a] not in NO_ESPERA: IDLE.add(NEXT[_a])
# esperas con contador: pc del bcc -> direccion del long que suma 1 en cada vuelta. La de cada cuadro: 0x338A
# addq.l #1,$fe8118 / 0x3390 tst.w $fe8114 / 0x3396 beq 0x338A (FE8118 solo da la estadistica de FE811C/FE8120, pero
# asi el estado es el mismo que dando todas las vueltas). Se guardan con los ciclos de una vuelta segun cycles()
ESPERAS = {0x3396: 0xFE8118}
for _b, _c in list(ESPERAS.items()):
    _i = INS[_b]; _h = _i.target
    assert _i.op == 'bcc' and _i.cond == 'eq' and INS[_h].op == 'addq' and INS[_h].sz == 4 and INS[_h].dst.v == _c and \
        INS[NEXT[_h]].op == 'tst' and NEXT[NEXT[_h]] == _b, 'espera %06x distinta de la prevista' % _b
    ESPERAS[_b] = (_c, cycles(INS[_h]) + cycles(INS[NEXT[_h]]) + cycles(_i))

# ---------------------------------------------------------------- bucles rapidos
# Bucle interno sencillo: cuerpo contiguo de H a B, B (bcc/dbcc/bra) vuelve a H, sin llamadas, sin otros saltos
# atras y sin tocar el SR. Ademas del codigo normal se genera una copia del cuerpo (F_H) donde:
# - los accesos por un An de induccion (solo cambia por (An)+, -(An), addq/subq/adda/suba/lea con constante, en
#   instrucciones que se hacen en todas las vueltas: paso fijo) van sin comprobar la region prevista: la entrada
#   (L_H) calcula cuantas vueltas fn caben en ella;
# - los Dn que en el cuerpo solo se usan como .b/.w llevan solo la parte baja (la alta queda en dhN);
# - el contador de ciclos va desplazado (fb) para que la comprobacion de cada vuelta sea un signo y salte tanto si
#   toca la IRQ como al pasar fn vueltas (cmin: ciclos minimos de una vuelta).
# Al salir (o al llegar a la vuelta fn) se deja todo como en el codigo normal; la vuelta normal (S_H) no vuelve a
# entrar en la copia. Los ciclos de cada bloque y los puntos de IRQ son los mismos
FAST_OPS = ('move', 'movea', 'moveq', 'lea', 'clr', 'tst', 'add', 'addi', 'addq', 'sub', 'subi', 'subq', 'addqa', 'subqa',
            'adda', 'suba', 'cmp', 'cmpi', 'cmpa', 'cmpm', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'not', 'neg', 'negx',
            'addx', 'subx', 'ext', 'swap', 'exg', 'mulu', 'muls', 'btst', 'bchg', 'bclr', 'bset', 'asl', 'asr', 'lsl', 'lsr',
            'rol', 'ror', 'roxl', 'roxr', 'scc', 'bcc', 'bra', 'nop', 'movem', 'move_from_sr', 'move_to_ccr')
SHIFTS = ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr')
REG_LO = {'ram': 0xFE0000, 'rom': 0, 'pal': 0x210000, 'vram': 0x200000, 'vregs': 0x218000}
REG_SZ = {'ram': 0x10000, 'rom': 0x100000, 'pal': 0x2000, 'vram': 0x10000, 'vregs': 0x1000}
# escrituras sin efectos. La VRAM tambien: el perfil (rc_regbit) no la da como region prevista a una instruccion que
# haya escrito en los registros del GAE1
PLAIN_W = ('ram', 'pal', 'vram', 'vregs')
NMAX = 0x10000
# se hace la copia si el bucle llega a RAFAGA ciclos del 68000 en un cuadro (cuadros pesados) o a MEDIA ciclos por
# cuadro de media en el banco de pruebas (cada copia ocupa sitio en la RAM de la PS1), y si da de media VMIN vueltas
# o mas cada vez que se entra (la entrada y la salida cuestan)
RAFAGA, MEDIA, VMIN = 8192, 512, 8

def operands(i):
    """operandos en el orden en que gen_ins los evalua: [(papel, ea, 'r' | 'w' | 'rw', tamano)]"""
    o, sz = i.op, i.sz
    if o == 'move': return [('src', i.src, 'r', sz), ('dst', i.dst, 'w', sz)]
    if o in ('movea', 'cmpa', 'adda', 'suba', 'mulu', 'muls', 'move_to_ccr'): return [('src', i.src, 'r', sz)]
    if o in ('clr', 'scc', 'move_from_sr'): return [('dst', i.dst, 'w', sz)]
    if o == 'tst': return [('dst', i.dst, 'r', sz)]
    if o in ('add', 'addi', 'addq', 'sub', 'subi', 'subq', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'addx', 'subx'):
        return [('src', i.src, 'r', sz), ('dst', i.dst, 'rw', sz)]
    if o in ('not', 'neg', 'negx') or o in SHIFTS: return [('dst', i.dst, 'rw', sz)]
    if o in ('cmp', 'cmpi', 'cmpm'): return [('src', i.src, 'r', sz), ('dst', i.dst, 'r', sz)]
    if o == 'btst': return [('dst', i.dst, 'r', 4 if i.dst.m == 'D' else 1)]
    if o in ('bchg', 'bclr', 'bset'): return [('dst', i.dst, 'rw', 4 if i.dst.m == 'D' else 1)]
    return []

class Loop: pass

def bucle(h, b, body, bcyc):
    ib = INS[b]; bs = set(body)
    skip = set(); targets = set()
    for a in body[:-1]:
        i = INS[a]
        if i.op not in FAST_OPS: return None
        if i.op in ('bcc', 'bra'):
            t = i.target
            if t in bs and t <= a: return None          # otro salto atras dentro (bucle interior)
            if t in bs:
                targets.add(t); skip.update(x for x in body if a < x < t)
            if i.op == 'bra' and t not in bs: skip.update(x for x in body if x > a)
    delta, nonind, acc, used, bad = {}, set(), [], set(), set()
    def mod(r, d, a):
        delta[r] = delta.get(r, 0) + d
        if a in skip: nonind.add(r)
    def dref(ea, size):
        if ea is None: return
        if ea.m == 'D':
            used.add(ea.r)
            if size == 4: bad.add(ea.r)
        if ea.m in ('idx', 'pci') and ea.x[0] == 'D':
            used.add(ea.x[1])
            if ea.x[2]: bad.add(ea.x[1])
    for a in body:
        i = INS[a]; o = i.op
        for role, ea, k, size in operands(i):
            dref(ea, size)
            if ea.m in ('ind', 'post', 'pre', 'd16'):
                r = ea.r; inc = 2 if (size == 1 and r == 7) else size
                if ea.m == 'pre': mod(r, -inc, a)
                off = delta.get(r, 0) + (ea.v if ea.m == 'd16' else 0)
                for kk in k: acc.append((a, role, kk, r, off, size))
                if ea.m == 'post': mod(r, inc, a)
        if o in ('btst', 'bchg', 'bclr', 'bset') or o in SHIFTS:
            if i.src.m == 'D': used.add(i.src.r)
        if o in ('mulu', 'muls', 'moveq', 'ext', 'swap'): used.add(i.dst.r); bad.add(i.dst.r)
        if o == 'dbcc': used.add(i.dst.r)
        if o == 'lea':
            dref(i.src, 2)
            if i.src.m == 'd16' and i.src.r == i.dst.r: mod(i.dst.r, i.src.v, a)
            elif not (i.src.m == 'ind' and i.src.r == i.dst.r): nonind.add(i.dst.r)
        if o in ('addqa', 'subqa', 'adda', 'suba'):
            if i.src.m == 'imm':
                v = i.src.v
                if i.sz == 2 and o in ('adda', 'suba'): v = (v & 0xFFFF) - (0x10000 if v & 0x8000 else 0)
                elif i.sz == 4: v = (v & 0xFFFFFFFF) - (1 << 32 if v & 0x80000000 else 0)
                mod(i.dst.r, v if o.startswith('add') else -v, a)
            else: nonind.add(i.dst.r)
        if o == 'movea': nonind.add(i.dst.r)
        if o == 'exg':
            for ea in (i.src, i.dst):
                (used.add(ea.r), bad.add(ea.r)) if ea.m == 'D' else nonind.add(ea.r)
        if o == 'movem':
            ea = i.dst if i.n == 0 else i.src
            regs = [('D', k) for k in range(8)] + [('A', k) for k in range(8)]
            rl = [regs[15 - bit] if ea.m == 'pre' else regs[bit] for bit in range(16) if i.extra & (1 << bit)]
            for kd, r in rl:
                if kd == 'D': used.add(r); bad.add(r)
                elif i.n: nonind.add(r)
            dref(ea, 4)
            nb = len(rl) * i.sz
            if ea.m == 'pre': mod(ea.r, -nb, a)
            if ea.m == 'post': mod(ea.r, nb, a)
    L = Loop()
    L.h, L.b, L.body, L.bodyset, L.targets = h, b, body, bs, targets
    L.defer_ok = all(INS[a].target in bs for a in body[:-1] if INS[a].op in ('bcc', 'bra'))
    L.split = sorted(used - bad)
    L.unchk = {}; groups = {}; L.al = set()
    for a, role, kk, r, off, size in acc:
        if r in nonind or a not in PRED: continue
        reg = PRED[a][0 if kk == 'r' else 1]
        # sin comprobar solo escrituras sin efectos en regiones sin huecos (la VRAM tiene dentro el GAE1)
        if reg not in REG_LO or (kk == 'w' and (reg not in PLAIN_W or reg == 'vram')): continue
        # long alineado a 4 en el perfil (RDA32): de una vez si la entrada ve la direccion alineada y el paso lo mantiene
        al = kk == 'r' and size == 4 and PRED[a][2] and delta.get(r, 0) % 4 == 0
        if al: L.al.add((r, off & 3))
        L.unchk.setdefault(a, {})[(role, kk)] = (reg, al)
        g = groups.setdefault((r, reg), [off, off]); g[0] = min(g[0], off); g[1] = max(g[1], off)
    L.groups = groups; L.step = delta
    L.cmin = sum(bcyc[a] for a in body if a in bcyc and a not in skip) + (DBCC_TOMADO if ib.op == 'dbcc' else 0)
    if not (L.unchk or L.split) or not 0 < L.cmin < 0x4000: return None
    L.mask = sum(1 << r for r in L.split)
    # los ciclos de los bloques que se hacen en todas las vueltas se suman de una vez en H; si se sale antes de
    # llegar a alguno, se quitan al salir
    L.always = sorted(a for a in body if a in bcyc and a not in skip)
    L.cyc0 = sum(bcyc[a] for a in L.always)
    L.rest = lambda a: ('cyr += fb - %d; ' % sum(bcyc[x] for x in L.always if x > a) if any(x > a for x in L.always)
                        else 'cyr += fb; ') + ''.join('d%d |= dh%d; ' % (r, r) for r in L.split)
    L.enter = ''.join('dh%d = d%d & 0xFFFF0000u; d%d &= 0xFFFFu; ' % (r, r, r) for r in L.split)
    return L

def bucles_rapidos(bcyc):
    loops = {}
    for b in ADDRS:
        ib = INS[b]
        if ib.op not in ('bcc', 'dbcc', 'bra') or ib.target > b or ib.target not in INS or b in IDLE or b in ESPERAS: continue
        if ib.op != 'bra' and NEXT[b] not in INS: continue
        h = ib.target; body = []; a = h
        while a in INS and a < b: body.append(a); a = NEXT[a]
        if a != b or h in loops: continue
        v = VUELTAS.get(b)
        if not v or not NCUADROS: continue
        L = bucle(h, b, body + [b], bcyc)
        nh = VUELTAS.get(h, (0,))[0]   # veces en H: entradas + saltos atras de B
        if L and (v[1] * L.cmin >= RAFAGA or v[0] * L.cmin >= MEDIA * NCUADROS) and nh >= VMIN * max(1, nh - v[2]):
            loops[h] = L
    return loops

def guarda(L):
    """entrada de la copia rapida: vueltas que caben en la region de cada An de induccion, contador desplazado"""
    s = ['{ u32 fn = 0x%xu, fx, fy;' % NMAX]
    for (r, reg), (omin, omax) in sorted(L.groups.items()):
        lo, n, st = REG_LO[reg], REG_SZ[reg], L.step.get(r, 0)
        s.append('fx = a%d + 0x%xu; fy = a%d + 0x%xu;' % (r, (omin - lo) & 0xFFFFFFFF, r, (omax - lo) & 0xFFFFFFFF))
        k = '(0x%xu - fy) / %du + 1' % (n - 1, st) if st > 0 else 'fx / %du + 1' % -st if st < 0 else None
        s.append('if (fx >= 0x%xu || fy >= 0x%xu) fn = 0;%s' % (n, n, ' else { fx = %s; if (fx < fn) fn = fx; }' % k if k else ''))
    for r, o in sorted(L.al): s.append('if ((a%d + %du) & 2) fn = 0;' % (r, o))
    # con cmin redondeado a potencia de 2 por abajo (desplazamiento): sale antes, nunca despues
    s.append('if (fn) { fb = cyr + (s32)(fn << %d); if (fb > 0) fb = 0; cyr -= fb; %sgoto F_%06x; } }'
             % (L.cmin.bit_length() - 1, L.enter, L.h))
    return ' '.join(s)

def block_cycles(labs):
    """ciclos sumados por bloque basico (al principio del bloque)"""
    CF = ('bcc', 'dbcc', 'bra', 'bsr', 'jsr', 'jmp', 'rts', 'rte')
    lead = set(labs)
    for a in ADDRS:
        if INS[a].op in CF: lead.add(NEXT[a])
    bcyc = {}; cur = None; cur_last = None
    for a in ADDRS:
        if a in lead or cur is None or NEXT.get(cur_last, -1) != a: cur = a; bcyc[cur] = 0
        bcyc[cur] += cycles(INS[a]); cur_last = a
    return bcyc

HN, HS = 4096, 17
# los perfiles (rc_regmask, regiones.py) guardan el pc en 16 bits de palabra
assert ADDRS[-1] < 0x20000, 'codigo 68k por encima de 0x20000: ampliar los perfiles'

def main(out):
    live_in, live_out = liveness()
    labs, taken = labels()
    f = open(out, 'w')
    w = f.write
    w('/* generado por rc68.py: World Rally 2 68000 -> C (uso personal) */\n#define RC_GEN 1\n#define RC_HN %d\n#define RC_HS %d\n' % (HN, HS))
    w('#include "rc_rt.h"\n#define RC_IRQ6 0x%xu\n' % IRQ6)
    w('const u32 rc_ntaken = %d;\n' % len(taken))
    w('RC_RUN_ATTR void rc_run(u32 start_pc)\n{\n    RC_LOCALS\n')
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
    bcyc = block_cycles(labs)
    loops = bucles_rapidos(bcyc)
    cx = Ctx(labs, live_out)
    for L in loops.values():
        cx.slow[L.b] = L.h
        if INS[L.b].op != 'bra': labs.add(NEXT[L.b])   # salida de la copia rapida
    def copia(L):
        # copia rapida del cuerpo (va justo antes de su entrada L_H: cerca en la cache de instrucciones)
        cf = Ctx(labs, live_out); cf.fast = L; cf.live_in = live_in
        out = ['    /* copia rapida del bucle %06x-%06x: %s */\n' % (L.h, L.b, ', '.join(['d%d.w' % r for r in L.split] +
               ['a%d %s %+d %d..%d' % (r, reg, L.step.get(r, 0), g[0], g[1]) for (r, reg), g in sorted(L.groups.items())]))]
        for a in L.body:
            i = INS[a]
            lines = gen_ins(i, live_out, labs, taken, cf)
            lab = 'F_%06x: ' % a if a == L.h or a in L.targets else ''
            cy = ' CYC(%d);' % (L.cyc0 if a == L.h else bcyc[a]) if a in bcyc and (a == L.h or a not in L.always) else ''
            lv = live_in[a] & ~cf.dflags if a == L.b else live_in[a]   # las aplazadas aun no estan escritas
            out.append('%sRC_FJ(%d); CK(0x%x, %d); RC_FS(%d);%s /* %r */\n' % (lab, L.mask, a, lv, L.mask, cy, i))
            out += ['    ' + l + '\n' for l in lines]
        return out
    for a in ADDRS:
        i = INS[a]
        lines = gen_ins(i, live_out, labs, taken, cx)
        lab = 'L_%06x: ' % a if a in labs else ''
        if a in loops:
            w('    goto L_%06x;\n' % a)
            for l in copia(loops[a]): w(l)
            lab = 'L_%06x: %s\nS_%06x: ' % (a, guarda(loops[a]), a)
        cy = ' CYC(%d);' % bcyc[a] if a in bcyc else ''
        w('%sCK(0x%x, %d);%s' % (lab, a, live_in[a], cy))
        if a in IDLE: w(' IDLEHINT(%s);' % COND_EXPR[i.cond])
        if a in ESPERAS: w(' IDLECNT(%s, 0x%x, %d);' % (COND_EXPR[i.cond], ESPERAS[a][0] - 0xFE0000, ESPERAS[a][1]))
        w(' /* %r */\n' % i)
        for l in lines: w('    ' + l + '\n')
    w('rc_k0:\n    dpc = 0; goto bad_dispatch;\n')
    w('    RC_EXIT_CODE\n}\n')
    f.close()
    print('instr', len(ADDRS), 'etiquetas', len(labs), 'despacho', len(taken), 'esperas', ' '.join('%x' % x for x in sorted(IDLE)), file=sys.stderr)
    print('bcc fusionados %d, bucles rapidos %d (%d sin comprobar region, %d con Dn partidos)' % (len(cx.fuse), len(loops),
          sum(1 for L in loops.values() if L.unchk), sum(1 for L in loops.values() if L.split)), file=sys.stderr)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'wr2_rc.c')
