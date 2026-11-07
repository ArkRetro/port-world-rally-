"""Recompilador estatico 8051 -> C de la tarea del DS5002FP de World Rally (una vuelta del bucle principal:
de 0x013F hasta volver a 0x013F). Uso personal.
Estado: A, CY, DPTR en variables globales del modulo; RAM interna y SFR en el mcs51_t del interprete
(se sincronizan al entrar y salir); xram = RAM compartida con el 68000 (byte a en a^1).
Banco de registros fijo (el codigo alcanzable nunca escribe PSW). Banderas: solo CY (como en el port Neo Geo)."""
import sys
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'walk51.py')).read().split("STOP={")[0])   # code, L, s8, succ
ENTRY = 0x013F
ENDS = {0x013F, 0x018C}       # volver al bucle principal

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
todo = [ENTRY]
while todo:
    f = todo.pop()
    if f in funcs: continue
    body, calls = reach(f, ENDS if f == ENTRY else set())
    funcs[f] = body
    todo.extend(calls)

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
    def jump(cond, t):
        if is_main and t in ENDS: tgt = 'return;'
        else: tgt = 'goto %s;' % lab(f, t)
        E('if (%s) %s' % (cond, tgt) if cond else tgt)
    for i, pc in enumerate(body):
        op = code[pc]; o1 = code[pc + 1]; o2 = code[pc + 2]; n = L[op]; nx = pc + n
        out.append('%s: /* %04x: %s */' % (lab(f, pc), pc, code[pc:pc + n].hex()))
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
        elif op == 0xE0: E('A = XR(DPTR);')
        elif op == 0xF0: E('XR(DPTR) = A;')
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

hdr = ['/* generado por rc51c.py: tarea del DS5002FP (8051) de World Rally -> C (uso personal) */',
       '#include "mcs51.h"',
       'typedef unsigned char u8; typedef unsigned short u16;',
       'static u8 A, CY, *IR, *SF, *XM; static u16 DPTR; static const u8 *CODE;',
       '#ifndef MCS51_XOR', '#define MCS51_XOR 0', '#endif',
       '#define XR(a) XM[((a) & 0x3FFF) ^ MCS51_XOR]']
for f in sorted(funcs):
    if f != ENTRY: hdr.append('static void mcu_f%04x(void);' % f)
for f in sorted(funcs): gen_func(f)
out.append('''/* una vuelta del bucle principal (de 0x013F a 0x013F), con el estado del interprete */
void mcu_run_c(mcs51_t *c)
{
    IR = c->iram; SF = c->sfr; XM = c->xram; CODE = c->code;
    A = c->sfr[0x60]; CY = c->sfr[0x50] >> 7; DPTR = (u16)((c->sfr[0x03] << 8) | c->sfr[0x02]);
    mcu_loop();
    c->sfr[0x60] = A; c->sfr[0x50] = (u8)((c->sfr[0x50] & 0x7F) | (CY << 7));
    c->sfr[0x02] = (u8)DPTR; c->sfr[0x03] = (u8)(DPTR >> 8);
    c->pc = 0x013F;
}''')
open(sys.argv[1] if len(sys.argv) > 1 else 'mcu_rc.c', 'w').write('\n'.join(hdr + out) + '\n')
print('funciones', len(funcs), 'instrucciones', sum(len(b) for b in funcs.values()), file=sys.stderr)
