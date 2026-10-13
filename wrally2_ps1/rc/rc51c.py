"""Recompilador estatico 8051 -> C de los trabajos del DS5002FP de World Rally 2 (copia de wrally_ps1/rc/rc51c.py:
en vez de una vuelta del bucle principal, las funciones de los dos trabajos que pide el 68000, que acaban en RET;
el resto del programa de la MCU va en C a mano, mcu.c). Comprobado contra MAME en 17 266 trabajos. Uso personal.
Estado: A, CY, DPTR en variables globales del modulo; RAM interna y SFR en el mcs51_t del interprete
(se sincronizan al entrar y salir); xram = RAM compartida con el 68000 (byte a en a^1).
Banco de registros fijo (el codigo alcanzable nunca escribe PSW). Banderas: solo CY (como en el port Neo Geo)."""
import sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import work
code = open(work('mcu.bin'), 'rb').read()[:0x8000]
L = [1]*256
for op in range(256):
    if op in (0x02,0x12,0x10,0x20,0x30,0x43,0x53,0x63,0x75,0x85,0x90,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,0xD5): L[op]=3
    elif op&0x1f in (0x01,0x11): L[op]=2
    elif op in (0x05,0x15,0x24,0x25,0x34,0x35,0x40,0x50,0x60,0x70,0x80,0x42,0x44,0x45,0x52,0x54,0x55,0x62,0x64,0x65,0x72,0x74,0x76,0x77,0x82,0x86,0x87,0x92,0x94,0x95,0xA0,0xA2,0xA6,0xA7,0xB0,0xB2,0xC0,0xC2,0xC5,0xD0,0xD2,0xE5,0xF5) or 0x78<=op<=0x7F or 0x88<=op<=0x8F or 0xA8<=op<=0xAF or 0xD8<=op<=0xDF: L[op]=2
def s8(x): return x-256 if x>127 else x
def succ(pc):
    op=code[pc]; n=L[op]; o1=code[pc+1] if pc+1<0x8000 else 0; o2=code[pc+2] if pc+2<0x8000 else 0
    nx=pc+n
    if op&0x1f==0x01: return [((nx)&0xF800)|((op&0xE0)<<3)|o1], None
    if op&0x1f==0x11: return [nx], ((nx)&0xF800)|((op&0xE0)<<3)|o1
    if op==0x02: return [(o1<<8)|o2], None
    if op==0x12: return [nx], (o1<<8)|o2
    if op in (0x22,0x32): return [], None
    if op==0x73: return ['IND'], None
    if op==0x80: return [(nx+s8(o1))&0xffff], None
    if op in (0x40,0x50,0x60,0x70) or 0xD8<=op<=0xDF: return [nx,(nx+s8(o1))&0xffff], None
    if op in (0x10,0x20,0x30,0xD5) or 0xB4<=op<=0xBF: return [nx,(nx+s8(o2))&0xffff], None
    return [nx], None

ENTRY = -1                    # sin bucle principal: solo las funciones de los trabajos (acaban en RET)
ENDS = set()
JOBS = {'a': (0x074D, 0x40E5)}           # el trabajo B (jugador 2) no se pide con un monitor: fuera (20 KB)

def reach(entry, stops):
    seen = set(); work = [entry]; calls = set(); first = True
    while work:
        pc = work.pop()
        if pc in seen or (pc in stops and not first): continue
        first = False
        seen.add(pc)
        nx, cl = succ(pc)
        if cl is not None: calls.add(cl)
        for t in nx:
            if t == 'IND': raise Exception('salto indirecto en %04x' % pc)
            work.append(t)
    return seen, calls

funcs = {}
todo = [f for j in JOBS.values() for f in j]
while todo:
    f = todo.pop()
    if f in funcs: continue
    body, calls = reach(f, ENDS if f == ENTRY else set())
    funcs[f] = body
    todo.extend(calls)

# ciclos maquina por opcode (mcs51_cycles de MAME): el trabajo cuenta los suyos en MC por bloque basico, para el
# modelo de tiempo de la MCU (mcu.c: cada escritura con su momento)
CYC = [int(x) for x in """1,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,
2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,
2,2,2,2,1,2,1,1,1,1,1,1,1,1,1,1, 2,2,2,2,4,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,1,2,4,1,2,2,2,2,2,2,2,2,2,2,
2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,1,1,1,2,1,1,2,2,2,2,2,2,2,2, 2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,
2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1""".replace('\n', ' ').split(',')]
assert len(CYC) == 256

def bloques(f, body):
    """ciclos de cada bloque basico de la funcion f, en su primera instruccion"""
    lead = {f}
    for pc in body:
        op = code[pc]; o1 = code[pc + 1]; o2 = code[pc + 2]; nx = pc + L[op]
        t = None
        if op == 0x02: t = (o1 << 8) | o2
        elif op & 0x1F == 0x01: t = (nx & 0xF800) | ((op & 0xE0) << 3) | o1
        elif op in (0x80, 0x40, 0x50, 0x60, 0x70) or 0xD8 <= op <= 0xDF: t = (nx + s8(o1)) & 0xFFFF
        elif op in (0xB4, 0xB5, 0xD5) or 0xB8 <= op <= 0xBF: t = (nx + s8(o2)) & 0xFFFF
        if t is not None or op in (0x22, 0x32): lead.add(nx)
        if t is not None: lead.add(t)
    cyc = {}; cur = None
    for pc in body:
        if pc in lead or cur is None or pc != prev_nx: cur = pc; cyc[cur] = 0
        cyc[cur] += CYC[code[pc]]; prev_nx = pc + L[code[pc]]
    return cyc

out = []
def E(s): out.append('    ' + s)
def lab(f, a): return 'L%04x_%04x' % (f, a)

def rd(a):
    if a < 0x80: return 'IR[%d]' % a
    if a == 0xE0: return 'A'
    if a == 0x82: return '(u8)DPTR'
    if a == 0x83: return '(u8)(DPTR >> 8)'
    if a in (0xD0, 0x81): raise Exception('SFR no soportado %x' % a)
    return 'SF[%d]' % (a - 0x80)
def wr(a, v):
    if a < 0x80: return 'IR[%d] = %s;' % (a, v)
    if a == 0xE0: return 'A = %s;' % v
    if a == 0x82: return 'DPTR = (DPTR & 0xFF00) | (u8)(%s);' % v
    if a == 0x83: return 'DPTR = (DPTR & 0x00FF) | ((u16)(u8)(%s) << 8);' % v
    if a in (0xD0, 0x81): raise Exception('SFR no soportado %x' % a)
    return 'SF[%d] = %s;' % (a - 0x80, v)

def gen_func(f):
    body = sorted(funcs[f])
    is_main = f == ENTRY
    name = 'mcu_loop' if is_main else 'mcu_f%04x' % f
    out.append('static void %s(void)\n{' % name)
    if f != body[0]: E('goto %s;' % lab(f, f))
    bc = bloques(f, body)
    def jump(cond, t):
        if is_main and t in ENDS: tgt = 'return;'
        else: tgt = 'goto %s;' % lab(f, t)
        E('if (%s) %s' % (cond, tgt) if cond else tgt)
    for i, pc in enumerate(body):
        op = code[pc]; o1 = code[pc + 1]; o2 = code[pc + 2]; n = L[op]; nx = pc + n
        out.append('%s: /* %04x: %s */' % (lab(f, pc), pc, code[pc:pc + n].hex()))
        if pc in bc: E('MC += %d;' % bc[pc])
        rel1 = (nx + s8(o1)) & 0xFFFF; rel2 = (nx + s8(o2)) & 0xFFFF
        term = False
        R = lambda k: 'IR[%d]' % k
        if op == 0x02: jump(None, (o1 << 8) | o2); term = True
        elif op & 0x1F == 0x01: jump(None, (nx & 0xF800) | ((op & 0xE0) << 3) | o1); term = True
        elif op == 0x12: E('mcu_f%04x();' % ((o1 << 8) | o2))
        elif op & 0x1F == 0x11: E('mcu_f%04x();' % ((nx & 0xF800) | ((op & 0xE0) << 3) | o1))
        elif op in (0x22, 0x32): E('return;'); term = True
        elif op == 0x80: jump(None, rel1); term = True
        elif op == 0x04: E('A++;')
        elif op == 0x14: E('A--;')
        elif op == 0x05: E(wr(o1, '%s + 1' % rd(o1)))
        elif op == 0x15: E(wr(o1, '%s - 1' % rd(o1)))
        elif 0x08 <= op <= 0x0F: E('%s++;' % R(op & 7))
        elif 0x18 <= op <= 0x1F: E('%s--;' % R(op & 7))
        elif op == 0x23: E('A = (u8)((A << 1) | (A >> 7));')
        elif op == 0x03: E('A = (u8)((A >> 1) | (A << 7));')
        elif op == 0x33: E('{ u8 c = A >> 7; A = (u8)((A << 1) | CY); CY = c; }')
        elif op == 0x13: E('{ u8 c = A & 1; A = (u8)((A >> 1) | (CY << 7)); CY = c; }')
        elif op in (0x24, 0x25) or 0x28 <= op <= 0x2F or op in (0x34, 0x35) or 0x38 <= op <= 0x3F:
            v = '%d' % o1 if op in (0x24, 0x34) else rd(o1) if op in (0x25, 0x35) else R(op & 7)
            c = ' + CY' if op >= 0x34 else ''
            E('{ u16 t = (u16)A + (u8)(%s)%s; CY = t >> 8; A = (u8)t; }' % (v, c))
        elif op in (0x94, 0x95) or 0x98 <= op <= 0x9F:
            v = '%d' % o1 if op == 0x94 else rd(o1) if op == 0x95 else R(op & 7)
            E('{ u16 t = (u16)A - (u8)(%s) - CY; CY = (t >> 8) & 1; A = (u8)t; }' % v)
        elif op == 0x40: jump('CY', rel1)
        elif op == 0x50: jump('!CY', rel1)
        elif op == 0x60: jump('A == 0', rel1)
        elif op == 0x70: jump('A != 0', rel1)
        elif op in (0x42, 0x52, 0x62): E(wr(o1, '%s %s A' % (rd(o1), {0x42: '|', 0x52: '&', 0x62: '^'}[op])))
        elif op in (0x43, 0x53, 0x63): E(wr(o1, '%s %s %d' % (rd(o1), {0x43: '|', 0x53: '&', 0x63: '^'}[op], o2)))
        elif op in (0x44, 0x54, 0x64): E('A %s= %d;' % ({0x44: '|', 0x54: '&', 0x64: '^'}[op], o1))
        elif op in (0x45, 0x55, 0x65): E('A %s= %s;' % ({0x45: '|', 0x55: '&', 0x65: '^'}[op], rd(o1)))
        elif 0x48 <= op <= 0x4F: E('A |= %s;' % R(op & 7))
        elif 0x58 <= op <= 0x5F: E('A &= %s;' % R(op & 7))
        elif 0x68 <= op <= 0x6F: E('A ^= %s;' % R(op & 7))
        elif op == 0x74: E('A = %d;' % o1)
        elif op == 0x75: E(wr(o1, '%d' % o2))
        elif 0x78 <= op <= 0x7F: E('%s = %d;' % (R(op & 7), o1))
        elif op == 0x84: E('{ u8 b = SF[0x70]; if (b) { u8 q = A / b, r = A % b; A = q; SF[0x70] = r; } } CY = 0;')
        elif op == 0xA4: E('{ u16 t = (u16)A * SF[0x70]; A = (u8)t; SF[0x70] = (u8)(t >> 8); } CY = 0;')
        elif op == 0x85: E(wr(o2, rd(o1)))
        elif 0x88 <= op <= 0x8F: E(wr(o1, R(op & 7)))
        elif 0xA8 <= op <= 0xAF: E('%s = %s;' % (R(op & 7), rd(o1)))
        elif op == 0x90: E('DPTR = 0x%04x;' % ((o1 << 8) | o2))
        elif op == 0x93: E('A = CODE[(u16)(A + DPTR) & 0x7FFF];')
        elif op == 0xA3: E('DPTR++;')
        elif op == 0xB3: E('CY ^= 1;')
        elif op == 0xC3: E('CY = 0;')
        elif op == 0xD3: E('CY = 1;')
        elif op == 0xB4: E('CY = A < %d;' % o1); jump('A != %d' % o1, rel2)
        elif op == 0xB5: E('{ u8 v = %s; CY = A < v; if (A != v) goto %s; }' % (rd(o1), lab(f, rel2)) if not (is_main and rel2 in ENDS) else '{ u8 v = %s; CY = A < v; if (A != v) return; }' % rd(o1))
        elif 0xB8 <= op <= 0xBF: E('CY = %s < %d;' % (R(op & 7), o1)); jump('%s != %d' % (R(op & 7), o1), rel2)
        elif 0xD8 <= op <= 0xDF: E('%s--;' % R(op & 7)); jump('%s != 0' % R(op & 7), rel1)
        elif op == 0xD5: E(wr(o1, '%s - 1' % rd(o1))); jump('%s != 0' % rd(o1), rel2)
        elif op == 0xE0: E('A = XRD(DPTR);')
        elif op == 0xF0: E('XW(DPTR, A);')
        elif op == 0xE4: E('A = 0;')
        elif op == 0xF4: E('A = (u8)~A;')
        elif op == 0xC4: E('A = (u8)((A << 4) | (A >> 4));')
        elif op == 0xE5: E('A = %s;' % rd(o1)) if o1 != 0xE0 else None
        elif 0xE8 <= op <= 0xEF: E('A = %s;' % R(op & 7))
        elif op == 0xF5: E(wr(o1, 'A'))
        elif 0xF8 <= op <= 0xFF: E('%s = A;' % R(op & 7))
        elif op == 0xC5: E('{ u8 t = %s; %s A = t; }' % (rd(o1), wr(o1, 'A')))
        elif 0xC8 <= op <= 0xCF: E('{ u8 t = %s; %s = A; A = t; }' % (R(op & 7), R(op & 7)))
        elif op == 0x00: pass
        else:
            raise Exception('opcode no soportado %02x en %04x' % (op, pc))
        if not term:
            if is_main and nx in ENDS: E('return;')
            elif i + 1 >= len(body) or body[i + 1] != nx:
                if nx not in funcs[f]: raise Exception('cae fuera en %04x' % pc)
                E('goto %s;' % lab(f, nx))
    out.append('}')

hdr = ['/* generado por rc51c.py: trabajos del DS5002FP (8051) de World Rally 2 -> C (uso personal) */',
       '#include "mcs51.h"',
       'typedef unsigned char u8; typedef unsigned short u16;',
       'static u8 A, CY, *IR, *SF, *XM; static u16 DPTR; static const u8 *CODE;',
       '#ifndef MCS51_XOR', '#define MCS51_XOR 0', '#endif',
       '#define XR(a) XM[((a) & 0x7FFF) ^ MCS51_XOR]   /* MOVX >= 8000h = 68000 FE8000-FEFFFF (los trabajos solo tocan FC1A-FDD5) */',
       '/* ciclos maquina del trabajo y escrituras con su momento (mcu.c, modelo con tiempo) */',
       'unsigned mcu_mc; int mcu_sellos; void mcu_log(unsigned short a, unsigned char v); unsigned char mcu_lee(unsigned short a);',
       '#define MC mcu_mc',
       '#define XRD(a) (mcu_sellos ? mcu_lee(a) : XR(a))',
       '#define XW(a, v) do { u16 xa_ = (a); u8 xv_ = (v); XR(xa_) = xv_; if (mcu_sellos) mcu_log(xa_, xv_); } while (0)']
for f in sorted(funcs):
    hdr.append('static void mcu_f%04x(void);' % f)
for f in sorted(funcs): gen_func(f)
for j, (f1, f2) in sorted(JOBS.items()):
    out.append('''/* trabajo %s (lcall %04X; lcall %04X), con el estado del interprete */
void mcu_job_%s(mcs51_t *c)
{
    IR = c->iram; SF = c->sfr; XM = c->xram; CODE = c->code;
    A = c->sfr[0x60]; CY = c->sfr[0x50] >> 7; DPTR = (u16)((c->sfr[0x03] << 8) | c->sfr[0x02]);
    mcu_f%04x(); mcu_f%04x();
    c->sfr[0x60] = A; c->sfr[0x50] = (u8)((c->sfr[0x50] & 0x7F) | (CY << 7));
    c->sfr[0x02] = (u8)DPTR; c->sfr[0x03] = (u8)(DPTR >> 8);
}''' % (j, f1, f2, j, f1, f2))
partes = [f for j, fs in sorted(JOBS.items()) for f in fs]
out.append('''/* una parte de un trabajo (0: %s), con el estado del interprete */
void mcu_part(mcs51_t *c, int k)
{
    IR = c->iram; SF = c->sfr; XM = c->xram; CODE = c->code;
    A = c->sfr[0x60]; CY = c->sfr[0x50] >> 7; DPTR = (u16)((c->sfr[0x03] << 8) | c->sfr[0x02]);
    switch (k) { %s }
    c->sfr[0x60] = A; c->sfr[0x50] = (u8)((c->sfr[0x50] & 0x7F) | (CY << 7));
    c->sfr[0x02] = (u8)DPTR; c->sfr[0x03] = (u8)(DPTR >> 8);
}''' % (' '.join('%d %04X' % (k, f) for k, f in enumerate(partes)), ' '.join('case %d: mcu_f%04x(); break;' % (k, f) for k, f in enumerate(partes))))
open(sys.argv[1] if len(sys.argv) > 1 else 'mcu_rc.c', 'w').write('\n'.join(hdr + out) + '\n')
print('funciones', len(funcs), 'instrucciones', sum(len(b) for b in funcs.values()), file=sys.stderr)
