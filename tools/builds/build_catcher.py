#!/usr/bin/env python3
# World Rally v6 -> EXCEPTION-CATCHER build (for real AES/MVS + NeoSD diagnostics)
#
# usage: build_catcher.py [base_neo] [out_neo] [item] [workdir]
#   base_neo : WRally_neogeo_final_v6.neo (P region at 0x1000, 0x200000 bytes, word-swapped)
#   out_neo  : output .neo for hardware
#   item     : MAME softlist item name (lowercase) ; roms/<item>/ and hash/neogeo.xml are written in workdir
#
# What it does (CPU addresses):
#  * vectors 2-9, 11-24, 28-63 -> per-vector 16-byte stub at 0x1200 + vec*16
#    (vectors 0,1,10,25,26,27 untouched; 0x1000-0x11FF left free for other builds)
#  * stub: move.w #$2700,sr / move.b #vec,VECL / jmp common.w
#  * common: kick watchdog, nested check (SP inside catcher stack), copy 32 bytes at (SP) to RAM
#    (memory->memory, no register touched), movem.l d0-d7/a0-a7 to RAM, USP, then SP := catcher stack,
#    decode frame (group-0 for vec 2/3), select cart fix ROM (0x3A001B), no shadow, Z80 cmd 3,
#    hide sprites (SCB3=0), clear fix map, palettes black + text white + backdrop per class,
#    print report, loop forever kicking the watchdog with an on-screen alive counter.
#  * screen (fix layer, cart S1 font: tile = ASCII code, colours 1+2 white; rows 4..27 double spaced, col 2):
#      WR CATCH VEC vv NAME          (vv = vector number in HEX, e.g. 0B = line F, 25 = TRAP 5)
#      PC pppppp  SR ssss  CNT cccc  (CNT = word $10F38C at catch time)
#      SP ssssss  USP uuuuuuuu       (SP = SSP before the exception = A7 at entry + frame size)
#      ADR aaaaaa  IR iiii  FC ffff  (vectors 2/3 only; FC = first frame word, low 5 bits R/W,I/N,FC)
#      D0..D7, A0..A7 (A7 = frame address), STK = 8 words above the frame, LOOP = alive counter
#      NEST nn VEC vv PC pppppp      (only if the catcher itself faulted)
#    backdrop: red = bus/addr/illegal/priv/trace/line-F/unassigned, orange = div0/CHK/TRAPV,
#              blue = spurious / IRQ4-7 autovector, purple = TRAP #n
#  * NOTE: byte write 0x3A001B = REG_CRTFIX (cart S1 fix tiles); 0x3A000B = REG_BRDFIX (MVS SFIX).
#    The AES has no SFIX, so the cart S1 (which holds the standard ASCII font at tiles 0x20-0x5A) is used.
# MAME test (item dir roms/<item>, hash dir hash/):
#   mame neogeo <item> -rompath "<workdir>/roms;<lab>/roms" -hashpath <workdir>/hash   (also aes, -bios unibios40)
import struct, sys, os, zlib

SP_ = '/tmp/claude-0/-home-user-port-world-rally-/307720d2-25b6-5a9f-8356-703187f15fbf/scratchpad'
LAB = SP_ + '/lab'
W = LAB + '/work/build-catcher'
base_neo = sys.argv[1] if len(sys.argv) > 1 else SP_ + '/rar/WRally_neogeo_final_v6.neo'
out_neo = sys.argv[2] if len(sys.argv) > 2 else SP_ + '/WRally_v6_catcher.neo'
ITEM = sys.argv[3] if len(sys.argv) > 3 else 'wrcatch'
WD = sys.argv[4] if len(sys.argv) > 4 else W

neo = bytearray(open(base_neo, 'rb').read())
assert neo[:4] == b'NEO\x01'
PSIZE = struct.unpack('<I', neo[4:8])[0]
assert PSIZE == 0x200000
src = bytes(neo[0x1000:0x1000 + PSIZE])
ref = open(LAB + '/roms/wrv6/wrv6-p1.p1', 'rb').read()
assert src == ref, 'base .neo P region differs from lab wrv6-p1.p1'
A = bytearray(len(src)); A[0::2] = src[1::2]; A[1::2] = src[0::2]   # CPU (big endian) view, file order

def off(a): return a if a < 0x100000 else a - 0x100000
def r16(a): return struct.unpack('>H', A[off(a):off(a)+2])[0]
def r32(a): return struct.unpack('>I', A[off(a):off(a)+4])[0]
def w32(a, v): A[off(a):off(a)+4] = struct.pack('>I', v)

# ---------------------------------------------------------------- constants
CODE = 0x1200            # stubs at CODE + vec*16 (vec 2..63) -> 0x1220..0x15FF ; common at 0x1600
COMMON = 0x1600
END_LIMIT = 0x2000
KEEP = {0, 1, 10, 25, 26, 27}
CATCH = [v for v in range(2, 64) if v not in KEEP]
# RAM (measured unused by v6 after its self-test in 3-min runs: neogeo/unibios40/aes, demo + play)
RAMB = 0x10D800
VECL = RAMB + 0x00       # byte: vector number written by stub
NEST = RAMB + 0x02       # word: nested exception count
NVEC = RAMB + 0x04       # long: nested vector
NPC = RAMB + 0x08        # long: nested PC
RAW = RAMB + 0x20        # 32 bytes copied from (SP) at entry
REGS = RAMB + 0x40       # D0-D7,A0-A7 (64 bytes)
USPV = RAMB + 0x80       # long
V = RAMB + 0x90          # decoded values (longs)
V_VEC, V_SR, V_PC, V_SP, V_CNT, V_ADR, V_IR, V_FC, V_G0 = [V + 4*i for i in range(9)]
V_STK = V + 36           # 8 longs
V_NAME = V + 68          # long: pointer to name string
V_BG = V + 72            # long: backdrop colour (low word)
CSTK_LO = 0x10DA00       # catcher stack range (nested detection)
CSTK = 0x10DBF0          # catcher SP
CNTW = 0x10F38C          # intro/frame counter word of the port
TXTPAL = 15              # fix palette used for text
BLANK = (TXTPAL << 12) | 0x20

# ---------------------------------------------------------------- mini assembler
class Asm:
    def __init__(s, base): s.base = base; s.items = []; s.labels = {}
    def L(s, name): s.items.append(('L', name))
    def raw(s, b): s.items.append(('B', bytes(b)))
    def w(s, *ws): s.raw(b''.join(struct.pack('>H', x & 0xFFFF) for x in ws))
    def l(s, *ls): s.raw(b''.join(struct.pack('>I', x & 0xFFFFFFFF) for x in ls))
    def br(s, op, label): s.items.append(('BR', op, label))      # op + 16-bit displacement (Bcc.w/BSR.w)
    def dbra(s, dn, label): s.items.append(('BR', 0x51C8 | dn, label))
    def absw_label(s, op, label): s.items.append(('AW', op, label))  # op + abs.w address of label
    def dw_label(s, label): s.items.append(('DW', label))           # data word = address of label
    @staticmethod
    def size(it): return {'L': 0, 'DW': 2}.get(it[0], len(it[1]) if it[0] == 'B' else 4)
    def assemble(s):
        pc = s.base
        for it in s.items:
            if it[0] == 'L': s.labels[it[1]] = pc
            pc += s.size(it)
        out = bytearray(); pc = s.base
        for it in s.items:
            if it[0] == 'B': out += it[1]
            elif it[0] == 'BR':
                d = s.labels[it[2]] - (pc + 2); assert -32768 <= d < 32768
                out += struct.pack('>Hh', it[1], d)
            elif it[0] == 'AW':
                t = s.labels[it[2]]; assert t < 0x8000
                out += struct.pack('>HH', it[1], t)
            elif it[0] == 'DW':
                t = s.labels[it[1]]; assert t < 0x8000
                out += struct.pack('>H', t)
            pc += s.size(it)
        assert len(out) == pc - s.base
        return bytes(out)

a = Asm(COMMON)
# ======================= common entry (no register may change before the movem) ===============
a.L('common')
a.w(0x13C0); a.l(0x300001)                    # move.b d0,$300001        kick watchdog
a.w(0xBFFC); a.l(CSTK_LO)                     # cmpa.l #CSTK_LO,a7
a.br(0x6500, 'first')                         # bcs.w first   (sp < lo)
a.w(0xBFFC); a.l(CSTK)                        # cmpa.l #CSTK,a7
a.br(0x6200, 'first')                         # bhi.w first   (sp > hi)
a.br(0x6000, 'nested')                        # bra.w nested
a.L('first')
a.w(0x23D7); a.l(RAW)                         # move.l (a7),RAW
for i in range(1, 8):
    a.w(0x23EF, 4*i); a.l(RAW + 4*i)          # move.l 4i(a7),RAW+4i
a.w(0x48F9, 0xFFFF); a.l(REGS)                # movem.l d0-d7/a0-a7,REGS
a.w(0x4E68)                                   # move.l usp,a0
a.w(0x23C8); a.l(USPV)                        # move.l a0,USPV
a.w(0x4FF9); a.l(CSTK)                        # lea CSTK,a7           (safe stack)
a.w(0x4279); a.l(NEST)                        # clr.w NEST
a.w(0x42B9); a.l(NVEC)                        # clr.l NVEC
a.w(0x42B9); a.l(NPC)                         # clr.l NPC
# ---- decode
a.w(0x41F9); a.l(RAW)                         # lea RAW,a0
a.w(0x43F9); a.l(V)                           # lea V,a1
a.w(0x7000)                                   # moveq #0,d0
a.w(0x1039); a.l(VECL)                        # move.b VECL,d0
a.w(0x0240, 0x003F)                           # andi.w #$3f,d0
a.w(0x2280)                                   # move.l d0,(a1)        V_VEC
a.w(0x7200)                                   # moveq #0,d1           G0 flag
a.w(0x0C00, 0x0002)                           # cmpi.b #2,d0
a.br(0x6700, 'g0')                            # beq.w g0
a.w(0x0C00, 0x0003)                           # cmpi.b #3,d0
a.br(0x6600, 'notg0')                         # bne.w notg0
a.L('g0')
a.w(0x7201)                                   # moveq #1,d1
a.w(0x7400)                                   # moveq #0,d2
a.w(0x3418)                                   # move.w (a0)+,d2       status word
a.w(0x2342, 28)                               # move.l d2,28(a1)      V_FC
a.w(0x2418)                                   # move.l (a0)+,d2       access address
a.w(0x2342, 20)                               # move.l d2,20(a1)      V_ADR
a.w(0x7400)                                   # moveq #0,d2
a.w(0x3418)                                   # move.w (a0)+,d2       IR
a.w(0x2342, 24)                               # move.l d2,24(a1)      V_IR
a.L('notg0')
a.w(0x2341, 32)                               # move.l d1,32(a1)      V_G0
a.w(0x7400)                                   # moveq #0,d2
a.w(0x3418)                                   # move.w (a0)+,d2       SR
a.w(0x2342, 4)                                # move.l d2,4(a1)       V_SR
a.w(0x2418)                                   # move.l (a0)+,d2       PC
a.w(0x2342, 8)                                # move.l d2,8(a1)       V_PC
a.w(0x2408)                                   # move.l a0,d2
a.w(0x0482); a.l(RAW)                         # subi.l #RAW,d2        frame size
a.w(0xD4B9); a.l(REGS + 60)                   # add.l REGS+60,d2      + A7 at entry = SP before exception
a.w(0x2342, 12)                               # move.l d2,12(a1)      V_SP
a.w(0x45E9, 36)                               # lea 36(a1),a2         V_STK
a.w(0x7607)                                   # moveq #7,d3
a.L('stk')
a.w(0x7400)                                   # moveq #0,d2
a.w(0x3418)                                   # move.w (a0)+,d2
a.w(0x24C2)                                   # move.l d2,(a2)+
a.dbra(3, 'stk')                              # dbra d3,stk
a.w(0x7400)                                   # moveq #0,d2
a.w(0x3439); a.l(CNTW)                        # move.w $10F38C,d2
a.w(0x2342, 16)                               # move.l d2,16(a1)      V_CNT
# name / colour from table (4 bytes per vector: name ptr word, colour word)
a.w(0xE548)                                   # lsl.w #2,d0
a.absw_label(0x41F8, 'vtab')                  # lea vtab.w,a0
a.w(0xD0C0)                                   # adda.w d0,a0
a.w(0x7400)                                   # moveq #0,d2
a.w(0x3418)                                   # move.w (a0)+,d2
a.w(0x2342, 68)                               # move.l d2,68(a1)      V_NAME
a.w(0x3410)                                   # move.w (a0),d2
a.w(0x2342, 72)                               # move.l d2,72(a1)      V_BG
a.br(0x6000, 'display')                       # bra.w display
# ======================= nested exception (inside the catcher) =====================================
a.L('nested')
a.w(0x7000)                                   # moveq #0,d0
a.w(0x1039); a.l(VECL)                        # move.b VECL,d0
a.w(0x23C0); a.l(NVEC)                        # move.l d0,NVEC
a.w(0x222F, 2)                                # move.l 2(a7),d1       PC (group 1/2 frame)
a.w(0x0C00, 0x0002)                           # cmpi.b #2,d0
a.br(0x6700, 'ng0')                           # beq.w ng0
a.w(0x0C00, 0x0003)                           # cmpi.b #3,d0
a.br(0x6600, 'nst')                           # bne.w nst
a.L('ng0')
a.w(0x222F, 10)                               # move.l 10(a7),d1      PC (group 0 frame)
a.L('nst')
a.w(0x23C1); a.l(NPC)                         # move.l d1,NPC
a.w(0x4FF9); a.l(CSTK)                        # lea CSTK,a7
a.w(0x5279); a.l(NEST)                        # addq.w #1,NEST
a.w(0x0C79, 0x0003); a.l(NEST)                # cmpi.w #3,NEST
a.br(0x6200, 'dead0')                         # bhi.w dead0           too many: just keep alive
# fall through into display
# ======================= display ===================================================================
a.L('display')
a.w(0x46FC, 0x2700)                           # move.w #$2700,sr
a.w(0x13C0); a.l(0x300001)                    # move.b d0,$300001     kick
a.w(0x49F9); a.l(0x3C0000)                    # lea $3C0000,a4        REG_VRAMADDR
a.w(0x4BF9); a.l(0x3C0002)                    # lea $3C0002,a5        REG_VRAMRW
a.w(0x13C0); a.l(0x3A0001)                    # move.b d0,$3A0001     REG_NOSHADOW
a.w(0x13C0); a.l(0x3A001B)                    # move.b d0,$3A001B     fix tiles from cart S1 (REG_CRTFIX)
a.w(0x13FC, 0x0003); a.l(0x320000)            # move.b #3,$320000     Z80 soft reset (silence)
a.w(0x397C, 0x0001, 0x0004)                   # move.w #1,4(a4)       REG_VRAMMOD = 1
a.w(0x38BC, 0x8200)                           # move.w #$8200,(a4)    SCB3
a.w(0x303C, 0x01FF)                           # move.w #$1ff,d0
a.w(0x7200)                                   # moveq #0,d1
a.L('hspr')
a.w(0x3A81)                                   # move.w d1,(a5)        height 0 -> sprite off
a.w(0x4E71)                                   # nop (spacing between VRAM writes)
a.dbra(0, 'hspr')
a.w(0x13C0); a.l(0x300001)                    # kick
a.w(0x38BC, 0x7000)                           # move.w #$7000,(a4)    fix map
a.w(0x303C, 0x04FF)                           # move.w #$4ff,d0
a.w(0x323C, BLANK)                            # move.w #BLANK,d1
a.L('cfix')
a.w(0x3A81)                                   # move.w d1,(a5)
a.w(0x4E71)                                   # nop
a.dbra(0, 'cfix')
a.w(0x13C0); a.l(0x300001)                    # kick
a.w(0x13C0); a.l(0x3A000F)                    # move.b d0,$3A000F     palette bank (one)
a.br(0x6100, 'setpal')                        # bsr.w setpal
a.w(0x13C0); a.l(0x3A001F)                    # move.b d0,$3A001F     palette bank (other, stays)
a.br(0x6100, 'setpal')                        # bsr.w setpal
a.w(0x397C, 0x0020, 0x0004)                   # move.w #$20,4(a4)     REG_VRAMMOD = 32 (next column)

def vram(col, row): return 0x7000 + col*32 + row
def at(col, row): a.w(0x38BC, vram(col, row))                 # move.w #addr,(a4)
strings = []
def text(col, row, s):
    lab = 'str%d' % len(strings); strings.append((lab, s))
    at(col, row); a.absw_label(0x41F8, lab); a.br(0x6100, 'prstr')   # lea str.w,a0 ; bsr prstr
def hexv(col, row, addr, n):
    if col is not None: at(col, row)
    a.w(0x2039); a.l(addr)                    # move.l addr,d0
    a.w(0x7200 | n)                           # moveq #n,d1
    a.br(0x6100, 'prhex')                     # bsr prhex

C0 = 2                                        # layout: rows 4..27 (double spaced), cols 2..36
def textc(s):                                 # text at current VRAM position
    lab = 'str%d' % len(strings); strings.append((lab, s))
    a.absw_label(0x41F8, lab); a.br(0x6100, 'prstr')
text(C0, 4, 'WR CATCH VEC '); hexv(None, 0, V_VEC, 2); textc(' ')
a.w(0x2079); a.l(V_NAME); a.br(0x6100, 'prstr')    # movea.l V_NAME,a0 ; bsr prstr
text(C0, 6, 'PC '); hexv(None, 0, V_PC, 6); textc('  SR '); hexv(None, 0, V_SR, 4)
textc('  CNT '); hexv(None, 0, V_CNT, 4)
text(C0, 8, 'SP '); hexv(None, 0, V_SP, 6); textc('  USP '); hexv(None, 0, USPV, 8)
a.w(0x4AB9); a.l(V_G0)                        # tst.l V_G0
a.br(0x6700, 'nog0')                          # beq.w nog0
text(C0, 10, 'ADR '); hexv(None, 0, V_ADR, 6); textc('  IR '); hexv(None, 0, V_IR, 4)
textc('  FC '); hexv(None, 0, V_FC, 4)
a.L('nog0')
for r, regs in enumerate([(0, 1, 2), (3, 4, 5), (6, 7)]):
    for k, i in enumerate(regs):
        text(C0 + 12*k, 12 + 2*r, 'D%d ' % i); hexv(None, 0, REGS + 4*i, 8)
        text(C0 + 12*k, 18 + 2*r, 'A%d ' % i); hexv(None, 0, REGS + 32 + 4*i, 8)
text(C0, 24, 'STK')
for i in range(8):
    if i % 4 == 0: at(C0 + 3, 24 + 2*(i//4))
    textc(' '); hexv(None, 0, V_STK + 4*i, 4)
text(C0 + 25, 26, 'LOOP ')
a.w(0x4A79); a.l(NEST)                        # tst.w NEST
a.br(0x6700, 'nonest')                        # beq.w nonest
text(C0, 27, 'NEST '); hexv(None, 0, NEST - 2, 2)    # long at NEST-2: low word = NEST
textc(' VEC '); hexv(None, 0, NVEC, 2); textc(' PC '); hexv(None, 0, NPC, 6)
a.L('nonest')
# ======================= forever: kick watchdog, alive counter ====================================
a.w(0x7E00)                                   # moveq #0,d7
a.L('loop')
a.w(0x13C0); a.l(0x300001)                    # move.b d0,$300001
a.w(0x5287)                                   # addq.l #1,d7
a.w(0x3007)                                   # move.w d7,d0
a.br(0x6600, 'loop')                          # bne.w loop
at(C0 + 30, 26)
a.w(0x2007)                                   # move.l d7,d0
a.w(0x4840)                                   # swap d0
a.w(0x7204)                                   # moveq #4,d1
a.br(0x6100, 'prhex')                         # bsr prhex
a.br(0x6000, 'loop')                          # bra.w loop
a.L('dead0')                                  # too many nested exceptions: keep alive only
a.w(0x13C0); a.l(0x300001)
a.br(0x6000, 'dead0')
# ======================= subroutines =============================================================
a.L('setpal')                                 # all palettes black, text palette white, backdrop
a.w(0x41F9); a.l(0x400000)                    # lea $400000,a0
a.w(0x303C, 0x0FFF)                           # move.w #$fff,d0
a.w(0x7200)                                   # moveq #0,d1
a.L('pclr')
a.w(0x30C1)                                   # move.w d1,(a0)+
a.dbra(0, 'pclr')
a.w(0x41F9); a.l(0x400000 + TXTPAL*32 + 2)    # lea text palette colour 1,a0
a.w(0x700E)                                   # moveq #14,d0
a.w(0x323C, 0x7FFF)                           # move.w #$7fff,d1     white
a.L('ptxt')
a.w(0x30C1)                                   # move.w d1,(a0)+
a.dbra(0, 'ptxt')
a.w(0x33F9); a.l(V_BG + 2); a.l(0x401FFE)     # move.w V_BG+2,$401FFE  backdrop
a.w(0x13C0); a.l(0x300001)                    # kick
a.w(0x4E75)                                   # rts
a.L('prstr')                                  # a0 = string (0-terminated), writes to (a5)
a.w(0x7400 | 0)                               # moveq #0,d2
a.L('ps1')
a.w(0x1418)                                   # move.b (a0)+,d2
a.br(0x6700, 'ps2')                           # beq.w ps2
a.w(0x0042, TXTPAL << 12)                     # ori.w #$F000,d2
a.w(0x3A82)                                   # move.w d2,(a5)
a.br(0x6000, 'ps1')                           # bra.w ps1
a.L('ps2')
a.w(0x4E75)                                   # rts
a.L('prhex')                                  # d0 = value, d1 = digits (1..8)
a.w(0x48E7, 0xF000)                           # movem.l d0-d3,-(a7)
a.w(0x7608)                                   # moveq #8,d3
a.w(0x9641)                                   # sub.w d1,d3
a.w(0xE54B)                                   # lsl.w #2,d3
a.w(0xE7A8)                                   # lsl.l d3,d0
a.w(0x5341)                                   # subq.w #1,d1
a.L('ph1')
a.w(0xE998)                                   # rol.l #4,d0
a.w(0x3400)                                   # move.w d0,d2
a.w(0x0242, 0x000F)                           # andi.w #$f,d2
a.w(0x0642, 0x0030)                           # addi.w #$30,d2
a.w(0x0C42, 0x003A)                           # cmpi.w #$3a,d2
a.br(0x6500, 'ph2')                           # bcs.w ph2
a.w(0x5E42)                                   # addq.w #7,d2
a.L('ph2')
a.w(0x0042, TXTPAL << 12)                     # ori.w #$F000,d2
a.w(0x3A82)                                   # move.w d2,(a5)
a.dbra(1, 'ph1')                              # dbra d1,ph1
a.w(0x4CDF, 0x000F)                           # movem.l (a7)+,d0-d3
a.w(0x4E75)                                   # rts
# ======================= data =====================================================================
NAMES = {2: 'BUS ERROR', 3: 'ADDRESS ERROR', 4: 'ILLEGAL INSTR', 5: 'ZERO DIVIDE', 6: 'CHK',
         7: 'TRAPV', 8: 'PRIVILEGE', 9: 'TRACE', 11: 'LINE F', 24: 'SPURIOUS IRQ'}
RED, ORANGE, BLUE, PURPLE, GREY = 0x0A00, 0x0A50, 0x000A, 0x0A0A, 0x0555
def vinfo(v):
    if v in (2, 3, 4, 8, 9, 11): return NAMES[v], RED
    if v in (5, 6, 7): return NAMES[v], ORANGE
    if v == 24: return NAMES[v], BLUE
    if 28 <= v <= 31: return 'IRQ%d AUTOVEC' % (v - 24), BLUE
    if 32 <= v <= 47: return 'TRAP %d' % (v - 32), PURPLE
    if 12 <= v <= 23 or 48 <= v <= 63: return 'UNASSIGNED', RED
    return 'UNKNOWN', GREY
a.L('vtab')
uniq = {}
for v in range(64):
    nm, col = vinfo(v)
    if nm not in uniq: uniq[nm] = 'nm%d' % len(uniq)
    a.dw_label(uniq[nm]); a.w(col)
for nm, lab in uniq.items():
    a.L(lab); a.raw(nm.encode() + b'\0' + (b'\0' if len(nm) % 2 == 0 else b''))
for lab, s in strings:
    a.L(lab); a.raw(s.encode() + b'\0' + (b'\0' if len(s) % 2 == 0 else b''))

code = a.assemble()
end = COMMON + len(code)
assert end <= END_LIMIT, hex(end)

# ---------------------------------------------------------------- stubs + vectors
assert all(x == 0xFF for x in A[0x1000:0x2000]), 'free area not 0xFF'
stubs = bytearray(b'\xff' * (COMMON - CODE))
for v in CATCH:
    st = struct.pack('>HH', 0x46FC, 0x2700) + struct.pack('>HHI', 0x13FC, v, VECL) + struct.pack('>HH', 0x4EF8, COMMON)
    assert len(st) == 16
    stubs[v*16:v*16+16] = st
A[CODE:COMMON] = stubs
A[COMMON:end] = code
orig_vec = {v: r32(v*4) for v in range(64)}
for v in CATCH:
    w32(v*4, CODE + v*16)
for v in KEEP:
    assert r32(v*4) == orig_vec[v]
# ---------------------------------------------------------------- outputs
p1 = bytearray(len(A)); p1[0::2] = A[1::2]; p1[1::2] = A[0::2]
diff = [i for i in range(len(src)) if src[i] != p1[i]]
neo2 = bytearray(neo); neo2[0x1000:0x1000 + PSIZE] = p1
open(out_neo, 'wb').write(neo2)
rd = f'{WD}/roms/{ITEM}'; os.makedirs(rd, exist_ok=True)
open(f'{rd}/{ITEM}-p1.p1', 'wb').write(p1)
for ext in ['s1', 'm1', 'v1', 'c1', 'c2']:
    d = f'{rd}/{ITEM}-{ext}.{ext}'
    if os.path.lexists(d): os.remove(d)
    os.symlink(f'{LAB}/roms/wrv6/wrv6-{ext}.{ext}', d)
hx = open(f'{LAB}/hash/neogeo.xml').read()
i0 = hx.index('<software name="wrv6">'); i1 = hx.index('</software>', i0) + len('</software>')
entry = hx[i0:i1].replace('wrv6', ITEM).replace('World Rally port v6', 'World Rally port v6 exception catcher')
os.makedirs(f'{WD}/hash', exist_ok=True)
i2 = hx.rindex('</softwarelist>')
open(f'{WD}/hash/neogeo.xml', 'w').write(hx[:i2] + '\t' + entry + '\n</softwarelist>\n')   # lab items + new item
labs = {k: hex(v) for k, v in a.labels.items() if not k.startswith(('str', 'nm'))}
print('code 0x%X-0x%X (%d bytes), stubs 0x%X-0x%X' % (COMMON, end, len(code), CODE + 2*16, CODE + 64*16))
print('labels', labs)
print('caught vectors', CATCH)
print('changed bytes', len(diff), 'file range', hex(min(diff)), hex(max(diff)))
print('p1 crc %08x  neo crc %08x  size %d -> %s' % (zlib.crc32(p1), zlib.crc32(neo2), len(neo2), out_neo))
