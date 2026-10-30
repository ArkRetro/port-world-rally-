* crypt.s - Gaelco VRAM "encryption" of Thunder Hoop (param1 0x0E, param2 0x4228) in software.
*
* The arcade video hardware decrypts every word the CPU writes to 0x100000-0x103FFF; on the Neo Geo that
* is plain work RAM, so each write site of the arcade program is replaced by an A-line opcode (0xA000 +
* site number, tools/mkrom.py) whose stub (generated stubs.s) does the write with the decrypted value.
* The game writes pairs with move.l: first word d1 = decrypt(0, 0, e1), second decrypt(e1, d1, e2)
* (MAME gaelcrpt.cpp; tools/mkcrypt.py builds the tables and checks this algorithm against it).

        .include "neogeo.i"
        .include "ram.i"

        .equ    P1,     0x0E

        .text
        .global decrypt_long, decrypt_word, linea_handler, linea_done, vstore
        .global fast_t1, fast_script, fast_cnt, fast_copy16, fast_movep, fast_fill, fast_rowcp

* decrypt_long: d0.l = encrypted pair (high word = first written) -> d0.l decrypted.  Others preserved.
decrypt_long:
        swap    d0
        tst.w   d0
        bne.s   1f
        /* high word 0 (counters, small values): the second word from a table (tools/mkrom.py CT_HI0) */
        swap    d0
        move.l  a0,-(sp)
        andi.l  #0xFFFF,d0
        add.l   d0,d0
        lea     CT_HI0,a0
        move.w  (a0,d0.l),d0
        swap    d0
        move.w  CT_T0,d0                /* first word: decrypt(0, 0, 0) */
        swap    d0
        movea.l (sp)+,a0
        rts
1:      swap    d0
        movem.l d1-d3/a0,-(sp)
        moveq   #0,d1
        swap    d0
        move.w  d0,d1                   /* e1 */
        swap    d0                      /* d0.w = e2 */
        add.l   d1,d1
        lea     CT_T0,a0
        move.w  (a0,d1.l),d2            /* d1 = first word decrypted */
        swap    d2                      /* kept in the high word of d2 */
        lea     CT_TP,a0
        move.w  (a0,d1.l),d3            /* TP[e1]: k1 | k2 part << 6 | swap << 11 | type << 13 */
        /* res = bitswap[swap](e2) ^ P2 */
        move.w  d3,d1
        andi.w  #0x1800,d1
        lsr.w   #1,d1                   /* swap * 1024 */
        lea     CT_BS,a0
        adda.w  d1,a0
        move.w  d0,d1
        lsr.w   #8,d1
        add.w   d1,d1
        move.w  (a0,d1.w),d2
        lea     512(a0),a0
        moveq   #0,d1
        move.b  d0,d1
        add.w   d1,d1
        move.w  (a0,d1.w),d1
        eor.w   d1,d2
        /* res = (res & 0xFFC0) | ((res + k1) & 0x3F); res ^= P1 */
        move.w  d3,d1
        add.w   d2,d1
        andi.w  #0x003F,d1
        andi.w  #0xFFC0,d2
        or.w    d1,d2
        eori.w  #P1,d2
        /* k2 = part from e1 ^ part from res (^ e2 bits 9 / 5 for type 0) */
        move.w  d3,d1
        lsr.w   #6,d1
        andi.w  #0x001F,d1
        rol.w   #4,d3
        andi.w  #6,d3                   /* type * 2 */
        bne.s   1f
        btst    #9,d0
        beq.s   2f
        bchg    #0,d1
2:      btst    #5,d0
        beq.s   1f
        bchg    #2,d1
1:      lsl.w   #6,d3                   /* type * 128 */
        lea     CT_KR,a0
        adda.w  d3,a0
        move.w  d2,d3
        andi.w  #0x003F,d3
        add.w   d3,d3
        move.w  (a0,d3.w),d3
        eor.w   d3,d1
        /* res = (res & 0x3F) | ((res + (k2 << 6)) & 0x7C0) | ((res + (k2 << 11)) & 0xF800) */
        move.w  d1,d3
        lsl.w   #6,d3
        add.w   d2,d3
        andi.w  #0x07C0,d3
        moveq   #11,d0
        lsl.w   d0,d1
        add.w   d2,d1
        andi.w  #0xF800,d1
        andi.w  #0x003F,d2
        or.w    d3,d2
        or.w    d1,d2
        eori.w  #((P1<<6)+(P1<<11))&0xFFFF,d2
        /* final bitswap */
        lea     CT_BF,a0
        move.w  d2,d1
        lsr.w   #8,d1
        add.w   d1,d1
        move.w  (a0,d1.w),d3
        lea     512(a0),a0
        moveq   #0,d1
        move.b  d2,d1
        add.w   d1,d1
        or.w    (a0,d1.w),d3
        move.l  d2,d0
        move.w  d3,d0
        movem.l (sp)+,d1-d3/a0
        rts

* decrypt_word: d0.w = a word written alone (move.w) -> d0.w decrypted.  Others preserved.
decrypt_word:
        move.l  a0,-(sp)
        andi.l  #0xFFFF,d0
        add.l   d0,d0
        lea     CT_T0,a0
        move.w  (a0,d0.l),d0
        move.l  (sp)+,a0
        rts

* ------------------------------------------------------------------------------------------
* Line A exception: 0xA000 + n at an arcade write site.  The stub of site n (stubs.s) finds the
* interrupted registers on the stack: d0-d7 at 0..28(sp), a0-a6 at 32..56(sp), SR at 60, PC at 62.
* It ends with d6 = length of the replaced instruction and d7 = the value moved (for the flags).
* ------------------------------------------------------------------------------------------
linea_handler:
        movem.l d0-d7/a0-a6,-(sp)
        movea.l 62(sp),a0
        move.w  (a0),d0
        andi.w  #0x0FFF,d0
        add.w   d0,d0
        add.w   d0,d0
        lea     linea_sites,a0
        movea.l (a0,d0.w),a0
        jmp     (a0)

linea_done:
        move.b  d0,REG_WATCHDOG         /* (the start-up VRAM tests run with the interrupts off) */
        add.l   d6,62(sp)               /* skip the replaced instruction */
        tst.l   d7                      /* move / clr: N and Z from the value, V = C = 0, X kept */
        move    sr,d6
        andi.w  #0x000F,d6
        andi.w  #0xFFF0,60(sp)
        or.w    d6,60(sp)
        movem.l (sp)+,d0-d7/a0-a6
        rte

* vstore: d0.l -> (a1) (arcade VRAM / screen RAM).  A tilemap cell that changes: mark its column.
vstore:
        cmp.l   (a1),d0
        beq.s   9f
        move.l  d0,(a1)
        move.l  a1,d1
        subi.l  #ARC_VRAM,d1
        cmpi.l  #0x2000,d1
        bcc.s   9f
        /* the row pair in the column's mask (RT_ROWD: layer * 64 + column * 2, bit = row / 2) */
        move.w  d1,d2
        lsr.w   #1,d2
        andi.w  #62,d2
        btst    #12,d1
        beq.s   2f
        addi.w  #64,d2
2:      lea     RT_ROWD,a0
        adda.w  d2,a0
        move.w  d1,d2
        lsr.w   #8,d2
        andi.w  #15,d2
        cmpi.w  #8,d2
        bcc.s   3f
        addq.l  #1,a0
3:      bset    d2,(a0)
        move.w  d1,d2
        lsr.w   #2,d2
        andi.w  #31,d2                  /* column */
        lea     rt_dirty0,a0
        btst    #12,d1
        beq.s   1f
        addq.l  #4,a0
1:      move.l  (a0),d1
        bset    d2,d1
        move.l  d1,(a0)
9:      rts

* ------------------------------------------------------------------------------------------
* Fast paths for the hottest write sites (jsr patched in by tools/mkrom.py instead of an A-line opcode:
* no exception, no register frame).  All write screen RAM (no tilemap column to mark).

* 0x512 (trap #1, once per task and frame): move.l a0,$102040.l.  d0 is free there (reloaded at 0x524)
fast_t1:
        move.l  a0,d0
        bsr     decrypt_long
        move.l  d0,ARC_SCREEN+0x040
        rts

* 0x1ABDA (script interpreter, once per opcode): move.l d0,$102c00.l (d0 is restored at 0x1ABE8)
fast_script:
        bsr     decrypt_long
        move.l  d0,ARC_SCREEN+0xC00
        rts

* 0x4FE (main loop): move.l (a6),$102810.l (followed by bra: the flags are not used)
fast_cnt:
        move.l  d0,-(sp)
        move.l  (a6),d0
        bsr     decrypt_long
        move.l  d0,ARC_SCREEN+0x810
        move.l  (sp)+,d0
        rts

* 0x16FD0-0x16FD9: move.w #$f,d0 / 1: move.l (a6)+,(a4)+ / dbra d0,1b  (16 longs ROM -> screen RAM)
* (the source is the ROM table at 0x19038: read from the decrypted shadow)
fast_copy16:
        move.l  a6,d0
        add.l   d0,d0
        addi.l  #CT_SHADOW,d0
        move.l  a5,-(sp)
        movea.l d0,a5
        moveq   #15,d0
1:      move.l  (a5),(a4)+
        addq.l  #8,a5
        dbra    d0,1b
        movea.l (sp)+,a5
        lea     64(a6),a6
        rts

* 0x11C46-0x11C4D (map block copy, once per cell): movep.l 0(a1),d3 / move.l d3,0(a0,d5.w).  a1 is in the
* map data in ROM: the decrypted long comes from CT_MOVEP (else decrypt_long); stored as vstore does
fast_movep:
        movep.l 0(a1),d3
        movem.l d0-d2/a0-a1,-(sp)
        move.l  a1,d0
        lea     (a0,d5.w),a1            /* destination */
        subi.l  #MOVEP_LO,d0
        cmpi.l  #MOVEP_N,d0
        bcc.s   1f
        lea     CT_MOVEP,a0
        move.l  (a0,d0.l),d0
        bra.s   2f
1:      move.l  d3,d0
        bsr     decrypt_long
2:      bsr     vstore
        movem.l (sp)+,d0-d2/a0-a1
        rts

* 0x8B0 / 0x8CC (a tilemap cleared): move.w #1023,d0 / 1: move.l d1,(a0)+ / dbra d0,1b.  d1 decrypted once;
* the columns that changed are marked (vstore)
fast_fill:
        movem.l d1-d3/a1,-(sp)
        move.l  d1,d0
        bsr     decrypt_long
        move.w  #1023,d3
1:      cmp.l   (a0),d0
        beq.s   2f
        movea.l a0,a1
        movem.l d0/a0,-(sp)
        bsr     vstore
        movem.l (sp)+,d0/a0
2:      addq.l  #4,a0
        dbra    d3,1b
        move.w  #0xFFFF,d0              /* as the dbra leaves it */
        movem.l (sp)+,d1-d3/a1
        rts

* 0x232FE-0x23305 (map rows from the ROM): move.l (a3),d7 / move.l d7,(a0) / lea 4(a0),a0
fast_rowcp:
        move.l  (a3),d7
        movem.l d0-d2/a0-a1,-(sp)
        move.l  d7,d0
        bsr     decrypt_long
        movea.l a0,a1
        bsr     vstore
        movem.l (sp)+,d0-d2/a0-a1
        addq.l  #4,a0
        rts
