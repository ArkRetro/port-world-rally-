#!/usr/bin/env python3
"""Black-box recorder on top of the P1-native v2 build.

Every VBlank the game writes a 20-word record into VRAM 0x8700-0x8713, an area no BIOS clears on a
reset.  At the next USER entry the record is shown on screen for ~14 s before the game starts, so
after a reset on real hardware the last state before the reset can be read off the screen:

  word  0-1  'WR' 'RC'   magic
  word  2    VBlank counter since the game started
  word  3    intro counter $10F38C
  word  4-5  PC interrupted by the VBlank
  word  6    SR at that point
  word  7-8  SP at that point
  word  9    flag word $10BF22 (unused, 0)
  word 10-11 PC interrupted by the last 240 Hz timer IRQ
  word 16    'JB' if the VBlank handler chained to the BIOS ($10BFFE lost the 'WR' marker)
  word 17    'EX' if an exception reached the error screen; word 18 = its vector number

Hooks: vector 25 -> REC_VBL (then the real VBlank handler 0x020CC0), vector 26 -> REC_TMR (then
0x0200D6), the BIOS fallback jump of the VBlank handler, the error-screen entry 0x236, and the USER
entry (shows the record, then jumps to the normal boot 0x020058).  All new code lives in the P1 hole
0x0A00-0x0BFF.  The display uses the HANDOVER hex printer at 0x35A and the cart S-ROM ASCII font.

usage: build_recorder.py v2.neo out.neo
"""
import struct, sys


def swap(b):
    o = bytearray(len(b)); o[0::2] = b[1::2]; o[1::2] = b[0::2]
    return bytes(o)


def w(x): return struct.pack('>H', x)
def l(x): return struct.pack('>I', x)
def movew_imm_abs(v, a): return w(0x33FC) + w(v) + l(a)
def movew_abs_abs(s, d): return w(0x33F9) + l(s) + l(d)
def jmp(a): return w(0x4EF9) + l(a)
KICK = w(0x13C0) + l(0x300001)                       # move.b d0,$300001
VRAM_ADDR, VRAM_DATA, VRAM_INC = 0x3C0000, 0x3C0002, 0x3C0004

REC_VBL, REC_TMR, REC_BIOS, REC_ERR, REC_SHOW, TABLE = 0xA00, 0xAA0, 0xAC0, 0xAE0, 0xB20, 0xC40
VBL_ORIG, TMR_ORIG, BOOT_ORIG = 0x020CC0, 0x0200D6, 0x020058
CNT, FLAG, TPC, BUF = 0x10BF20, 0x10BF22, 0x10BF24, 0x10BF40


def rec_vbl():
    c = w(0x0C79) + w(0x5752) + l(0x10BFFE)          # cmpi.w #'WR',$10BFFE  (game running?)
    c += w(0x6600) + w(0)                            # bne.w -> jmp VBL_ORIG (patched below)
    c += w(0x48E7) + w(0xC080)                       # movem.l d0-d1/a0,-(a7)   -> frame: 12(a7)=SR 14(a7)=PC
    c += movew_imm_abs(0x8700, VRAM_ADDR) + movew_imm_abs(1, VRAM_INC)
    c += movew_imm_abs(0x5752, VRAM_DATA) + movew_imm_abs(0x5243, VRAM_DATA)
    c += w(0x5279) + l(CNT)                          # addq.w #1,CNT
    c += movew_abs_abs(CNT, VRAM_DATA) + movew_abs_abs(0x10F38C, VRAM_DATA)
    c += w(0x33EF) + w(14) + l(VRAM_DATA)            # move.w 14(a7),VRAM_DATA   PC hi
    c += w(0x33EF) + w(16) + l(VRAM_DATA)            # PC lo
    c += w(0x33EF) + w(12) + l(VRAM_DATA)            # SR
    c += w(0x200F) + w(0x4840) + w(0x33C0) + l(VRAM_DATA) + w(0x4840) + w(0x33C0) + l(VRAM_DATA)   # SP hi, lo
    c += movew_abs_abs(FLAG, VRAM_DATA) + movew_abs_abs(TPC, VRAM_DATA) + movew_abs_abs(TPC + 2, VRAM_DATA)
    c += w(0x4CDF) + w(0x0103)                       # movem.l (a7)+,d0-d1/a0
    c = bytearray(c + jmp(VBL_ORIG))
    c[10:12] = w(len(c) - 6 - 10)                    # bne target = the final jmp
    return bytes(c)


def rec_tmr():
    return w(0x23EF) + w(2) + l(TPC) + jmp(TMR_ORIG)   # move.l 2(a7),TPC ; jmp timer


def rec_bios():
    # mark 'JB' only if the game had already been running (VBlank counter != 0; the BIOS clears RAM at boot)
    c = w(0x4A79) + l(CNT) + w(0x6700) + w(18)                                   # tst.w CNT ; beq.w -> the jmp (disp from the extension word)
    c += movew_imm_abs(0x8710, VRAM_ADDR) + movew_imm_abs(0x4A42, VRAM_DATA)   # 16 bytes
    return c + jmp(0xC00438)


def rec_err():
    c = w(0x46FC) + w(0x2700) + KICK                 # the two instructions replaced at 0x236
    c += movew_imm_abs(0x8711, VRAM_ADDR) + movew_imm_abs(1, VRAM_INC) + movew_imm_abs(0x4558, VRAM_DATA)
    c += movew_abs_abs(0x10BF10, VRAM_DATA) + jmp(0x240)
    return c


def rec_show():
    c = KICK + w(0x46FC) + w(0x2700)
    # read 20 words from VRAM 0x8700.. into BUF (reads do not auto-increment)
    c += w(0x41F9) + l(BUF) + w(0x323C) + w(0x8700) + w(0x7413)          # lea BUF,a0 ; move.w #$8700,d1 ; moveq #19,d2
    loop = w(0x33C1) + l(VRAM_ADDR) + w(0x4E71) + w(0x30F9) + l(VRAM_DATA) + w(0x5241)   # move.w d1,ADDR ; nop ; move.w DATA,(a0)+ ; addq.w #1,d1
    c += loop + w(0x51CA) + w(-len(loop) - 2 & 0xFFFF)                    # dbra d2,loop
    c += w(0x0C79) + w(0x5752) + l(BUF)                                   # cmpi.w #'WR',BUF
    bne1 = len(c); c += w(0x6600) + w(0)
    c += w(0x0C79) + w(0x5243) + l(BUF + 2)
    bne2 = len(c); c += w(0x6600) + w(0)
    # display setup (same as the HANDOVER error screen)
    c += movew_imm_abs(0, 0x3C0006) + movew_imm_abs(7, 0x3C000C) + movew_imm_abs(1, VRAM_INC)
    c += movew_imm_abs(0x8200, VRAM_ADDR) + w(0x303C) + w(0x1FF)
    lp = movew_imm_abs(0, VRAM_DATA) + w(0x4E71) + w(0x4E71); c += lp + w(0x51C8) + w(-len(lp) - 2 & 0xFFFF)
    c += movew_imm_abs(0x7000, VRAM_ADDR) + w(0x303C) + w(0x4FF)
    lp = movew_imm_abs(0xF020, VRAM_DATA) + KICK; c += lp + w(0x51C8) + w(-len(lp) - 2 & 0xFFFF)
    c += movew_imm_abs(0x7FFF, 0x4001E2) + movew_imm_abs(0, 0x401FFE) + movew_imm_abs(0x20, VRAM_INC)
    # print 20 words, 4 hex digits each, via the HANDOVER printer at 0x35A (d0 value, d1 digits-1)
    c += w(0x43F9) + l(TABLE) + w(0x41F9) + l(BUF) + w(0x7613)            # lea TABLE,a1 ; lea BUF,a0 ; moveq #19,d3
    lp = w(0x33D9) + l(VRAM_ADDR) + w(0x4E71) + w(0x3018) + w(0x7203) + w(0x4EB9) + l(0x35A)   # move.w (a1)+,ADDR ; nop ; move.w (a0)+,d0 ; moveq #3,d1 ; jsr $35A
    c += lp + w(0x51CB) + w(-len(lp) - 2 & 0xFFFF)
    # wait ~9 s kicking the watchdog, then clear the magic
    c += w(0x7AFA)                                                        # moveq #-6,d5 -> use as counter 250 via d5=... simpler: move.w #250,d5
    c = c[:-2] + w(0x3A3C) + w(400)
    outer = w(0x323C) + w(0x4000) + KICK + w(0x51C9) + w(-len(KICK) - 2 & 0xFFFF)
    c += outer + w(0x51CD) + w(-len(outer) - 2 & 0xFFFF)
    c += movew_imm_abs(0x8700, VRAM_ADDR) + movew_imm_abs(0, VRAM_DATA)
    skip = len(c)
    c += jmp(BOOT_ORIG)
    c = bytearray(c)
    for at in (bne1, bne2):
        c[at + 2:at + 4] = w(skip - (at + 2))
    return bytes(c)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    neo = bytearray(open(src, 'rb').read())
    img = bytearray(swap(neo[0x1000:0x201000]))
    assert img[0x97C:0x2000] == b'\xff' * (0x2000 - 0x97C), 'P1 hole 0x97C-0x1FFF not free'
    assert struct.unpack('>I', img[25 * 4:25 * 4 + 4])[0] == VBL_ORIG and struct.unpack('>I', img[26 * 4:26 * 4 + 4])[0] == TMR_ORIG
    assert img[0x22E:0x234] == jmp(BOOT_ORIG) and img[0x236:0x240] == w(0x46FC) + w(0x2700) + KICK
    assert img[0x20CCA:0x20CD0] == jmp(0xC00438)
    pieces = {REC_VBL: rec_vbl(), REC_TMR: rec_tmr(), REC_BIOS: rec_bios(), REC_ERR: rec_err(), REC_SHOW: rec_show()}
    table = b''.join(w(0x7000 + (2 + 5 * (i % 5)) * 0x20 + 6 + 2 * (i // 5)) for i in range(20))
    pieces[TABLE] = table
    addrs = sorted(pieces)
    for a, b in zip(addrs, addrs[1:]):
        assert a + len(pieces[a]) <= b, 'overlap at %X' % a
    assert TABLE + len(table) <= 0xD00
    for a, code in pieces.items():
        img[a:a + len(code)] = code
    img[25 * 4:25 * 4 + 4] = l(REC_VBL); img[26 * 4:26 * 4 + 4] = l(REC_TMR)
    img[0x22E:0x234] = jmp(REC_SHOW)
    img[0x236:0x23C] = jmp(REC_ERR)
    img[0x20CCA:0x20CD0] = jmp(REC_BIOS)
    neo[0x1000:0x201000] = swap(img)
    neo[0x2C:0x2C + 33] = b'WRally P1native v2 RECORDER'.ljust(33, b'\0')
    open(dst, 'wb').write(neo)
    print('recorder installed:', ' '.join('%s@%03X(%d)' % (n, a, len(pieces[a])) for n, a in
          (('VBL', REC_VBL), ('TMR', REC_TMR), ('BIOS', REC_BIOS), ('ERR', REC_ERR), ('SHOW', REC_SHOW), ('TABLE', TABLE))))


if __name__ == '__main__':
    main()
