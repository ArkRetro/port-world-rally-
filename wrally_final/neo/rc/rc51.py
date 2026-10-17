"""Recompilador estatico 8051 -> 68000 para la tarea del DS5002FP de World Rally.
Registros: d7=A, d6=CY (0/0xFF), d5=DPTR (long, parte alta 0), a5=iram, a4=sfr (0x80..), a2=ROM 8051, a1=xram (0x10C000).
Banco de registros fijo (el codigo alcanzable nunca escribe PSW). Llamadas -> jbsr/rts."""
import sys, pickle
sys.path.insert(0, '/home/claude/wrally/neo/rc')
code = open('/home/claude/wrally/rom/wrdallas.bin', 'rb').read()[:0x8000]
exec(open('/home/claude/wrally/neo/rc/walk.py').read().split("STOP={")[0])   # L, s8, succ
ENTRY = 0x0171
STOP = {0x013F, 0x018C}
seen, calls, ind = pickle.load(open('/home/claude/wrally/neo/rc/walk.pkl', 'rb'))
assert not ind
out = []
def E(s): out.append('        ' + s)
def lab(a): return 'L51_%04x' % a
def jump(cond, t):
    if t in STOP:
        if cond == 'bra': E('rts')
        else:
            # salto condicional a un punto de parada: invertir y saltar sobre un rts
            inv = {'beq': 'bne', 'bne': 'beq'}[cond]
            E('%s.s 1f' % inv); E('rts'); out.append('1:')
    else:
        E('j%s %s' % (cond, lab(t)))
def rd(a, reg='%d0'):
    """lee el byte de direccion directa a en reg"""
    if a < 0x80: E('move.b %d(%%a5),%s' % (a, reg))
    elif a == 0xE0: E('move.b %%d7,%s' % reg)
    elif a == 0x82: E('move.b %%d5,%s' % reg)
    elif a == 0x83: E('move.w %%d5,%s' % reg); E('lsr.w #8,%s' % reg)
    elif a in (0xD0, 0x81): raise Exception('SFR no soportado %x' % a)
    else: E('move.b %d(%%a4),%s' % (a - 0x80, reg))
def wr(a, reg='%d0'):
    if a < 0x80: E('move.b %s,%d(%%a5)' % (reg, a))
    elif a == 0xE0: E('move.b %s,%%d7' % reg)
    elif a == 0x82: E('move.b %s,%%d5' % reg)
    elif a == 0x83: E('ror.w #8,%d5'); E('move.b %s,%%d5' % reg); E('ror.w #8,%d5')
    elif a in (0xD0, 0x81): raise Exception('SFR no soportado %x' % a)
    else: E('move.b %s,%d(%%a4)' % (reg, a - 0x80))
def ea(a):
    """operando directo como ea si es memoria simple, si no None"""
    if a < 0x80: return '%d(%%a5)' % a
    if a not in (0xE0, 0x82, 0x83, 0xD0, 0x81): return '%d(%%a4)' % (a - 0x80)
    return None
def src(a):
    e = ea(a)
    if e: return e
    rd(a); return '%d0'

addrs = sorted(seen)
out.append('        .text')
out.append('        .globl mcu_rc_entry')
out.append('mcu_rc_entry:')
E('jbra %s' % lab(ENTRY))
for i, pc in enumerate(addrs):
    op = code[pc]; o1 = code[pc + 1]; o2 = code[pc + 2]; n = L[op]; nx = pc + n
    out.append('%s:' % lab(pc))
    out.append('| %04x: %s' % (pc, code[pc:pc + n].hex()))
    rel1 = (nx + s8(o1)) & 0xFFFF; rel2 = (nx + s8(o2)) & 0xFFFF
    term = False
    if op == 0x02: jump('bra', (o1 << 8) | o2); term = True
    elif op & 0x1F == 0x01: jump('bra', (nx & 0xF800) | ((op & 0xE0) << 3) | o1); term = True
    elif op == 0x12: E('jbsr %s' % lab((o1 << 8) | o2))
    elif op & 0x1F == 0x11: E('jbsr %s' % lab((nx & 0xF800) | ((op & 0xE0) << 3) | o1))
    elif op == 0x22: E('rts'); term = True
    elif op == 0x80: jump('bra', rel1); term = True
    elif op == 0x04: E('addq.b #1,%d7')
    elif op == 0x14: E('subq.b #1,%d7')
    elif op == 0x05:
        e = ea(o1)
        if e: E('addq.b #1,%s' % e)
        else: rd(o1); E('addq.b #1,%d0'); wr(o1)
    elif 0x08 <= op <= 0x0F: E('addq.b #1,%d(%%a5)' % (op & 7))
    elif 0x18 <= op <= 0x1F: E('subq.b #1,%d(%%a5)' % (op & 7))
    elif op == 0x23: E('rol.b #1,%d7')
    elif op == 0x33: E('add.b %d6,%d6'); E('roxl.b #1,%d7'); E('scs %d6')
    elif op == 0x24: E('add.b #%d,%%d7' % o1); E('scs %d6')
    elif op == 0x25: E('add.b %s,%%d7' % src(o1)); E('scs %d6')
    elif 0x28 <= op <= 0x2F: E('add.b %d(%%a5),%%d7' % (op & 7)); E('scs %d6')
    elif op in (0x34, 0x35) or 0x38 <= op <= 0x3F or op in (0x94, 0x95) or 0x98 <= op <= 0x9F:
        if op in (0x34, 0x94): E('move.b #%d,%%d0' % o1)
        elif op in (0x35, 0x95): rd(o1)
        else: E('move.b %d(%%a5),%%d0' % (op & 7))
        E('add.b %d6,%d6')
        E(('addx.b' if op < 0x80 else 'subx.b') + ' %d0,%d7')
        E('scs %d6')
    elif op == 0x24: pass
    elif op == 0x40: E('tst.b %d6'); jump('bne', rel1)
    elif op == 0x50: E('tst.b %d6'); jump('beq', rel1)
    elif op == 0x60: E('tst.b %d7'); jump('beq', rel1)
    elif op == 0x70: E('tst.b %d7'); jump('bne', rel1)
    elif op in (0x43, 0x53):
        ins = 'ori.b' if op == 0x43 else 'andi.b'
        e = ea(o1)
        if e: E('%s #%d,%s' % (ins, o2, e))
        else: rd(o1); E('%s #%d,%%d0' % (ins[:-3] + '.b', o2)); wr(o1)
    elif op == 0x54: E('and.b #%d,%%d7' % o1)
    elif op == 0x65: rd(o1); E('eor.b %d0,%d7')
    elif op == 0x74: E('move.b #%d,%%d7' % o1)
    elif op == 0x75:
        e = ea(o1)
        if e: E('move.b #%d,%s' % (o2, e))
        else: E('move.b #%d,%%d0' % o2); wr(o1)
    elif 0x78 <= op <= 0x7F: E('move.b #%d,%d(%%a5)' % (o1, op & 7))
    elif op == 0x84:
        E('moveq #0,%d0'); E('move.b %d7,%d0'); E('moveq #0,%d1'); E('move.b 0x70(%a4),%d1')
        E('beq.s 1f'); E('divu.w %d1,%d0'); E('move.b %d0,%d7'); E('swap %d0'); E('move.b %d0,0x70(%a4)')
        out.append('1:'); E('clr.b %d6')
    elif op == 0xA4:
        E('moveq #0,%d0'); E('move.b %d7,%d0'); E('moveq #0,%d1'); E('move.b 0x70(%a4),%d1')
        E('mulu.w %d1,%d0'); E('move.b %d0,%d7'); E('lsr.w #8,%d0'); E('move.b %d0,0x70(%a4)'); E('clr.b %d6')
    elif op == 0x85:
        es, ed = ea(o1), ea(o2)
        if es and ed: E('move.b %s,%s' % (es, ed))
        else: rd(o1); wr(o2)
    elif 0x88 <= op <= 0x8F:
        e = ea(o1)
        if e: E('move.b %d(%%a5),%s' % (op & 7, e))
        else: E('move.b %d(%%a5),%%d0' % (op & 7)); wr(o1)
    elif op == 0x90: E('move.l #%d,%%d5' % ((o1 << 8) | o2))
    elif op == 0x93:
        E('moveq #0,%d0'); E('move.b %d7,%d0'); E('add.w %d5,%d0'); E('and.w #0x7FFF,%d0'); E('move.b (%a2,%d0.w),%d7')
    elif op == 0xA3: E('addq.w #1,%d5')
    elif op == 0xB3: E('not.b %d6')
    elif op == 0xB4: E('cmp.b #%d,%%d7' % o1); E('scs %d6'); jump('bne', rel2)
    elif 0xB8 <= op <= 0xBF: E('cmpi.b #%d,%d(%%a5)' % (o1, op & 7)); E('scs %d6'); jump('bne', rel2)
    elif op == 0xC3: E('clr.b %d6')
    elif 0xD8 <= op <= 0xDF: E('subq.b #1,%d(%%a5)' % (op & 7)); jump('bne', rel1)
    elif op == 0xE0: E('move.w %d5,%d0'); E('and.w #0x3FFF,%d0'); E('move.b (%a1,%d0.w),%d7')
    elif op == 0xF0: E('move.w %d5,%d0'); E('and.w #0x3FFF,%d0'); E('move.b %d7,(%a1,%d0.w)')
    elif op == 0xE4: E('clr.b %d7')
    elif op == 0xE5:
        e = ea(o1)
        if e: E('move.b %s,%%d7' % e)
        else: rd(o1, '%d7') if o1 != 0xE0 else None
    elif 0xE8 <= op <= 0xEF: E('move.b %d(%%a5),%%d7' % (op & 7))
    elif op == 0xF5: wr(o1, '%d7')
    elif 0xF8 <= op <= 0xFF: E('move.b %%d7,%d(%%a5)' % (op & 7))
    else:
        raise Exception('opcode no soportado %02x en %04x' % (op, pc))
    # caida al siguiente
    if not term:
        if nx in STOP: E('rts')
        elif i + 1 >= len(addrs) or addrs[i + 1] != nx:
            assert nx in seen, hex(nx)
            E('jbra %s' % lab(nx))
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
print('ok', len(addrs), 'instrucciones 8051')
