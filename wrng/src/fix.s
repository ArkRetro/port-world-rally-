* fix.s - fix layer text helpers (font at ASCII positions in the S ROM, palette PAL_FIX)

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global fix_print, fix_hex32, fix_hex16, fix_hex8, fix_addr

* fix_addr: d0 = x (0..39), d1 = y (0..31) -> REG_VRAMADDR set, VRAMMOD = 32 (next column)
fix_addr:
        move.w  d0,-(sp)
        lsl.w   #5,d0
        add.w   d1,d0
        add.w   #VRAM_FIX,d0
        move.w  d0,REG_VRAMADDR
        move.w  #32,REG_VRAMMOD
        move.w  (sp)+,d0
        rts

* fix_print: a0 = zero-terminated string, d0 = x, d1 = y
fix_print:
        bsr     fix_addr
        moveq   #0,d2
1:      move.b  (a0)+,d2
        beq.s   2f
        or.w    #(PAL_FIX<<12)&0xFFFF,d2
        move.w  d2,REG_VRAMRW
        bra.s   1b
2:      rts

* fix_hex32: d2.l value at d0,d1 ; fix_hex16: d2.w ; fix_hex8: d2.b
fix_hex32:
        bsr     fix_addr
        moveq   #7,d3
        bra.s   hex_loop
fix_hex16:
        bsr     fix_addr
        swap    d2
        moveq   #3,d3
        bra.s   hex_loop
fix_hex8:
        bsr     fix_addr
        ror.l   #8,d2
        moveq   #1,d3
hex_loop:
        rol.l   #4,d2
        moveq   #15,d4
        and.w   d2,d4
        move.b  hex_digits(pc,d4.w),d4
        or.w    #(PAL_FIX<<12)&0xFFFF,d4
        move.w  d4,REG_VRAMRW
        dbra    d3,hex_loop
        rts
hex_digits:
        .ascii  "0123456789ABCDEF"
        .align  2
