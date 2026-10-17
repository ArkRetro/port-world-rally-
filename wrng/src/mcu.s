* mcu.s - 68000 reimplementation of the DS5002FP program of World Rally (wrdallas.bin).
* See analysis/INFORME_MCU.md.  The MCU only (1) answers the presence signature, (2) interprets a
* per-stage script of position triggers and (3) runs an autopilot state machine.  All its variables
* live in the shared RAM (arcade FEC000-FEFFFF, here ARC_WRAM); MCU address Xxxx <-> ARC_WRAM + (Xxxx & 0x3FFF).
*
* mcu_frame replaces `move.w #1,$FEC102` at 0x3634: the original raised a request that the MCU served
* within the same frame; here it is served synchronously.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global mcu_frame

        .equ    MCUV,   ARC_WRAM - 0xC000       /* MCU addresses are 0xC000-0xFFFF: 68k address = MCUV + addr */

        .equ    C101,   MCUV+0xC101
        .equ    C100W,  MCUV+0xC100
        .equ    C102W,  MCUV+0xC102
        .equ    C105,   MCUV+0xC105
        .equ    C10E,   MCUV+0xC10E
        .equ    C149,   MCUV+0xC149
        .equ    C14B,   MCUV+0xC14B
        .equ    C16C,   MCUV+0xC16C             /* .w offset in the stage script */
        .equ    C16F,   MCUV+0xC16F
        .equ    C1A1,   MCUV+0xC1A1
        .equ    C1AA,   MCUV+0xC1AA             /* .w */
        .equ    C1B3,   MCUV+0xC1B3
        .equ    C1C7,   MCUV+0xC1C7
        .equ    C1CA,   MCUV+0xC1CA             /* .l score */
        .equ    C1FC,   MCUV+0xC1FC
        .equ    C20D,   MCUV+0xC20D
        .equ    C20F,   MCUV+0xC20F
        .equ    C215,   MCUV+0xC215
        .equ    C217,   MCUV+0xC217
        .equ    C218,   MCUV+0xC218             /* .w target x */
        .equ    C21A,   MCUV+0xC21A             /* .w target y */
        .equ    C21D,   MCUV+0xC21D
        .equ    C229,   MCUV+0xC229
        .equ    C22B,   MCUV+0xC22B
        .equ    C22D,   MCUV+0xC22D
        .equ    C22F,   MCUV+0xC22F
        .equ    C231,   MCUV+0xC231
        .equ    C233,   MCUV+0xC233
        .equ    C244,   MCUV+0xC244             /* .l */
        .equ    C252,   MCUV+0xC252             /* .l */
        .equ    C268,   MCUV+0xC268             /* .l */
        .equ    C26C,   MCUV+0xC26C             /* .l */
        .equ    C271,   MCUV+0xC271
        .equ    C273,   MCUV+0xC273
        .equ    C275,   MCUV+0xC275
        .equ    C277,   MCUV+0xC277
        .equ    C27B,   MCUV+0xC27B
        .equ    EA97,   MCUV+0xEA97
        .equ    EA98,   MCUV+0xEA98             /* .w */
        .equ    EA9C,   MCUV+0xEA9C             /* .w */
        .equ    EAA0,   MCUV+0xEAA0             /* .w car x */
        .equ    EAA4,   MCUV+0xEAA4             /* .w car y */
        .equ    EAA8,   MCUV+0xEAA8             /* .l vx */
        .equ    EAAC,   MCUV+0xEAAC             /* .l vy */
        .equ    EAB1,   MCUV+0xEAB1             /* heading 0..39 */
        .equ    EABC,   MCUV+0xEABC             /* .w heading*30 */
        .equ    EAC5,   MCUV+0xEAC5
        .equ    EAC7,   MCUV+0xEAC7
        .equ    EAD1,   MCUV+0xEAD1
        .equ    EAD2,   MCUV+0xEAD2             /* .w speed */
        .equ    EADB,   MCUV+0xEADB
        .equ    EAE9,   MCUV+0xEAE9
        .equ    EAEB,   MCUV+0xEAEB
        .equ    EAED,   MCUV+0xEAED
        .equ    EAF1,   MCUV+0xEAF1
        .equ    EAF2,   MCUV+0xEAF2             /* .l */
        .equ    EAF6,   MCUV+0xEAF6             /* .l */
        .equ    EAFA,   MCUV+0xEAFA             /* .l */
        .equ    EAFF,   MCUV+0xEAFF
        .equ    EB00,   MCUV+0xEB00             /* .w */
        .equ    EB02,   MCUV+0xEB02             /* .l */
        .equ    EB09,   MCUV+0xEB09
        .equ    EB0B,   MCUV+0xEB0B
        .equ    EB0D,   MCUV+0xEB0D
        .equ    EB1B,   MCUV+0xEB1B
        .equ    EB1C,   MCUV+0xEB1C             /* .w checkpoint x */
        .equ    EB1E,   MCUV+0xEB1E             /* .w checkpoint y */
        .equ    EB20,   MCUV+0xEB20             /* .w */
        .equ    EB4F,   MCUV+0xEB4F
        .equ    EB51,   MCUV+0xEB51
        .equ    EBD7,   MCUV+0xEBD7
        .equ    EBDD,   MCUV+0xEBDD
        .equ    EBE5,   MCUV+0xEBE5
        .equ    EBE7,   MCUV+0xEBE7
        .equ    F379,   MCUV+0xF379
        .equ    F37B,   MCUV+0xF37B
        .equ    F4FF,   MCUV+0xF4FF

* offsets inside the wrdallas.bin image
        .equ    T_ZONA, 0x07EC
        .equ    T_ATAN_PP, 0x0810               /* dx>=0, dy>=0 */
        .equ    T_ATAN_PN, 0x083C               /* dx>=0, dy<0  */
        .equ    T_ATAN_NN, 0x0868               /* dx<0,  dy<0  */
        .equ    T_ATAN_NP, 0x0894               /* dx<0,  dy>=0 */
        .equ    T_SIN,  0x08C0
        .equ    T_FLAG, 0x3FA4
        .equ    T_BASES, 0x4044
        .equ    T_MULT, 0x405E

* ------------------------------------------------------------------------------------------
mcu_frame:
        movem.l d0-d7/a0-a6,-(sp)
        clr.w   C102W                   /* C103 = 0 (request consumed) */
        clr.w   C100W                   /* C101 = 0 (busy) */
        bsr     mcu_script
        bsr     mcu_autopilot
        move.w  #1,C100W                /* C101 = 1 (ready) */
        movem.l (sp)+,d0-d7/a0-a6
        rts

* ------------------------------------------------------------------------------------------
* Script interpreter (MCU 0x3824)
* ------------------------------------------------------------------------------------------
mcu_script:
        tst.b   C1C7
        bne     ms_ret
        lea     mcu_rom,a6
        moveq   #0,d0
        move.b  C16F,d0
        add.w   d0,d0
        lea     T_BASES(a6),a1
        move.w  (a1,d0.w),d1            /* base (big-endian word) */
        move.w  C16C,d2                 /* off */
        lea     (a6,d1.w),a0
        adda.w  d2,a0                   /* a0 = p (d1 < 0x8000 so the index is fine) */
        moveq   #0,d3
        move.b  (a0),d3                 /* b0 */
        moveq   #0,d4
        move.b  1(a0),d4                /* b1 */
        move.b  2(a0),d5                /* cmd */
        move.w  d3,d6
        and.w   #0x3F,d6
        lsl.w   #8,d6
        or.w    d4,d6                   /* thr */
        move.w  d3,d0
        and.w   #0xC0,d0
        beq   ms_ax00
        cmpi.w  #0x80,d0
        beq   ms_ax80
        cmpi.w  #0x40,d0
        beq   ms_ax40
        /* 0xC0: pos = EAA4 ; sign bit from b0 bit5 */
        move.w  EAA4,d7
        btst    #5,d3
        beq   ms_cmp
        or.w    #0xC000,d6
        bra   ms_cmp
ms_ax00:
        move.w  EA98,d7
        add.w   #0x30,d7
        and.w   #0x0FFF,d7
        bra   ms_cmp
ms_ax80:
        move.w  EA9C,d7
        add.w   #0x18,d7
        and.w   #0x3FFF,d7
        bra   ms_cmp
ms_ax40:
        move.w  EAA0,d7
        add.w   #0x18,d7
ms_cmp:
        /* fire if (cmd & 0x80) ? pos >= thr : pos < thr  (signed) */
        tst.b   d5
        bmi     ms_ge
        cmp.w   d6,d7                   /* (after the tst: it would overwrite the flags) */
        bge     ms_ret                  /* pos >= thr -> no fire */
        bra     ms_run
ms_ge:
        cmp.w   d6,d7
        blt     ms_ret
ms_run:
        moveq   #2,d3                   /* q */
ms_loop:
        moveq   #0,d5
        move.b  (a0,d3.w),d5            /* c */
        move.w  d5,d0
        and.w   #0x60,d0
        cmpi.w  #0x60,d0
        beq   ms_end
        cmpi.w  #0x40,d0
        beq   ms_skip
        move.w  d5,d0
        and.w   #0x1F,d0
        lea     1(a0,d3.w),a1           /* a1 -> p0 */
        /* the command routines use d2/d3/d5/a0 as scratch (RACE_START's VELOCIDAD walks a0 through the
           sine table, RACE_START2's division runs in d2): keep the interpreter's offset, index, command
           byte and script pointer, or the script resumes at a garbage offset after every jump */
        movem.l d2-d3/d5/a0/a6,-(sp)
        bsr     ms_exec                 /* d0 = h (extra bytes); d0 = -1 if the command stored the offset itself */
        movem.l (sp)+,d2-d3/d5/a0/a6    /* (movem leaves the flags of ms_exec's result) */
        bmi     ms_ret
        add.w   d0,d3
        btst    #5,d5
        beq   ms_last
        addq.w  #2,d3
        bra   ms_loop
ms_last:
        addq.w  #2,d3
        add.w   d3,d2
        move.w  d2,C16C
        rts
ms_end:
        clr.w   C16C
        rts
ms_skip:
        addq.w  #4,d2
        move.w  d2,C16C
ms_ret:
        rts

* ms_exec: d0 = command k, a1 -> p0, d2 = off, d3 = q.  Returns d0 = h (>=0) or -1 with the flags set
* from d0.  May use any register but d0's result: the caller keeps d2, d3, d5, a0, a6.
ms_exec:
        cmpi.w  #0x0C,d0
        bhi     cmd_sign                /* 0x0D-0x1F = SIGN */
        add.w   d0,d0
        move.w  cmd_tab(pc,d0.w),d0
        jmp     cmd_tab(pc,d0.w)
cmd_tab:
        .word   cmd_sign-cmd_tab        /* 0 */
        .word   cmd_1-cmd_tab
        .word   cmd_setdir-cmd_tab
        .word   cmd_gotopt-cmd_tab
        .word   cmd_racestart-cmd_tab
        .word   cmd_racestart2-cmd_tab
        .word   cmd_nop-cmd_tab         /* 6 */
        .word   cmd_finish-cmd_tab
        .word   cmd_checkpoint-cmd_tab
        .word   cmd_9-cmd_tab
        .word   cmd_progress-cmd_tab    /* A */
        .word   cmd_light-cmd_tab       /* B */
        .word   cmd_nop-cmd_tab         /* C */

cmd_sign:                               /* C273=p0 ; C27B=FLAG[p0] ; C271=3 ; C275=p2 rol 3 ; C277=p1 rol 3 */
        moveq   #0,d0
        move.b  (a1),d0
        move.b  d0,C273
        lea     T_FLAG(a6),a2
        move.b  (a2,d0.w),C27B
        move.b  #3,C271
        move.b  2(a1),d0
        rol.b   #3,d0
        move.b  d0,C275
        move.b  1(a1),d0
        rol.b   #3,d0
        move.b  d0,C277
        moveq   #2,d0
        rts
cmd_1:
        move.b  #1,C271
        moveq   #0,d0
        rts
cmd_setdir:                             /* C22F=p0&0x3F ; EB1B=C22F ; C217=0 ; C229=2 ; EB4F=0 ; EB51=0 ; C231=0 */
        move.b  (a1),d0
        and.b   #0x3F,d0
        move.b  d0,C22F
        move.b  d0,EB1B
        clr.b   C217
        move.b  #2,C229
        clr.b   EB4F
        clr.b   EB51
        clr.b   C231
        moveq   #0,d0
        rts
cmd_gotopt:                             /* C21D=p0&3 ; C22D=0 ; C229=1 ; C215=p0>>7 ; C218=p1p2 ; C21A=p3p4 ; EB51++ */
        move.b  (a1),d0
        move.b  d0,d1
        and.b   #3,d1
        move.b  d1,C21D
        clr.b   C22D
        move.b  #1,C229
        clr.b   C215
        tst.b   d0
        bpl   1f
        move.b  #1,C215
1:      move.b  1(a1),C218
        move.b  2(a1),C218+1
        move.b  3(a1),C21A
        move.b  4(a1),C21A+1
        addq.b  #1,EB51
        moveq   #4,d0
        rts
cmd_racestart:                          /* p0 = (a1), p2 = 2(a1) */
        cmpi.b  #1,EB0B
        bne   1f
        move.b  #1,EB0D
        bra   rs_fin
1:      move.b  (a1),EAED
        move.b  #1,EAE9
        moveq   #0,d0
        move.b  C22F,d0
        move.b  d0,EAB1
        move.b  d0,EA97
        move.b  d0,EADB
        mulu    #30,d0
        move.w  d0,EABC
        clr.b   EBD7
        clr.b   EB09
        move.b  #1,C22D
        clr.b   EAD1
        clr.b   EAC5
        clr.b   EAC7
        clr.l   EAF2
        bsr     mult_index              /* d0 = ((EBDD+4)/10)*4 */
        lea     T_MULT(a6),a2
        move.w  (a2,d0.w),d1            /* k */
        moveq   #0,d4
        move.b  2(a1),d4                /* p2 */
        bsr     mul_k_p2                /* d1 = (k*p2)>>8 */
        moveq   #0,d4
        move.w  EAD2,d4
        mulu    d4,d1
        move.l  d1,EAF6
rs_fin:
        bsr     velocidad
        moveq   #2,d0
        rts
cmd_racestart2:
        clr.b   C22D
        cmpi.b  #1,EB0D
        bne   1f
        clr.b   EB0D
        bra   rs2_fin
1:      move.b  (a1),EAF1
        clr.b   EAE9
        move.b  #0x87,EBE7
        move.b  #1,EBE5
        clr.b   EAFF
        move.b  #1,EAEB
        move.l  EAF2,EB02
        move.l  EAF6,d2
        move.w  #12000,d3
        bsr     div32x16                /* d2.w = quotient */
        move.w  d2,EB00
        bsr     mult_index
        addq.w  #2,d0
        lea     T_MULT(a6),a2
        move.w  (a2,d0.w),d1
        moveq   #0,d4
        move.b  2(a1),d4
        bsr     mul_k_p2
        moveq   #0,d4
        move.w  EAD2,d4
        mulu    d4,d1
        move.l  d1,EAFA
rs2_fin:
        moveq   #2,d0
        rts
cmd_nop:
        moveq   #0,d0
        rts
cmd_finish:                             /* C1B3=0 ; bonus ; C1C7=2 ; C268=C244 ; C26C=C252 */
        clr.b   C1B3
        tst.b   F4FF
        bne   1f
        move.w  #0x0E0A,d0
        sub.w   C1AA,d0
        bcs   1f                      /* borrow -> no bonus */
        and.l   #0xFFFF,d0
        move.l  d0,d1
        add.l   d0,d0
        add.l   d1,d0                   /* t*3 */
        add.l   d0,C1CA
1:      move.b  #2,C1C7
        move.l  C244,C268
        move.l  C252,C26C
        moveq   #0,d0
        rts
cmd_checkpoint:                         /* EB1C = p1p2-0x18 ; EB1E = p3p4 ; off += q+6 ; C16C = EB20 = off */
        move.b  1(a1),d0
        lsl.w   #8,d0
        move.b  2(a1),d0
        sub.w   #0x18,d0
        move.w  d0,EB1C
        move.b  3(a1),d0
        lsl.w   #8,d0
        move.b  4(a1),d0
        move.w  d0,EB1E
        move.w  d2,d0
        add.w   d3,d0
        addq.w  #6,d0
        move.w  d0,C16C
        move.w  d0,EB20
        moveq   #-1,d0
        rts
cmd_9:
        move.b  #1,C105
        moveq   #0,d0
        rts
cmd_progress:                           /* C14B=p0 ; C149=1 */
        move.b  (a1),C14B
        move.b  #1,C149
        moveq   #0,d0
        rts
cmd_light:                              /* C1A1=p0 ; v=0455 ; p0!=0: v=0277, a=F379, a==2: v=0165 ; else a=0 ; C10E[((3a)+F37B)*4] = v,v */
        move.b  (a1),d0
        move.b  d0,C1A1
        move.w  #0x0455,d1
        moveq   #0,d4
        tst.b   d0
        beq   1f
        move.w  #0x0277,d1
        move.b  F379,d4
        cmpi.b  #2,d4
        bne   1f
        move.w  #0x0165,d1
1:      move.b  d4,d0
        add.b   d4,d4
        add.b   d0,d4                   /* 3a */
        add.b   F37B,d4
        lsl.b   #2,d4
        and.w   #0xFF,d4
        lea     C10E,a2
        move.w  d1,(a2,d4.w)
        move.w  d1,2(a2,d4.w)
        moveq   #0,d0
        rts

* mult_index: d0 = ((EBDD + 4) / 10) * 4  (8-bit arithmetic)
mult_index:
        moveq   #0,d0
        move.b  EBDD,d0
        addq.b  #4,d0
        and.w   #0xFF,d0
        divu    #10,d0
        and.w   #0xFF,d0
        lsl.w   #2,d0
        rts

* mul_k_p2: d1.w = k, d4.w = p2 -> d1.w = (k*p2)>>8 computed as hi(k_lo*p2) + k_hi*p2 (16 bits)
mul_k_p2:
        moveq   #0,d0
        move.b  d1,d0                   /* k_lo */
        mulu    d4,d0
        lsr.w   #8,d0
        lsr.w   #8,d1                   /* k_hi */
        mulu    d4,d1
        add.w   d0,d1
        rts

* div32x16: d2.l = dividend, d3.w = divisor -> d2.w = 16-bit quotient (restoring division, 16 steps,
* like the MCU routine at 0x079C), d2 high word = remainder.
div32x16:
        move.w  d4,-(sp)
        moveq   #15,d4
1:      lsl.l   #1,d2
        swap    d2
        cmp.w   d3,d2
        bcs   2f
        sub.w   d3,d2
        swap    d2
        addq.w  #1,d2
        bra   3f
2:      swap    d2
3:      dbra    d4,1b
        move.w  (sp)+,d4
        rts

* ------------------------------------------------------------------------------------------
* Autopilot (MCU 0x018E)
* ------------------------------------------------------------------------------------------
mcu_autopilot:
        lea     mcu_rom,a6
        clr.b   C22B
        move.b  C229,d0
        beq     ap_ret
        cmpi.b  #2,d0
        beq     ap_state2
        /* ---- state 1: go to point (C218, C21A) ---- */
        move.w  EAA0,d0
        add.w   #0x18,d0
        sub.w   C218,d0                 /* dx */
        move.w  EAA4,d1
        sub.w   C21A,d1                 /* dy */
        move.w  d0,d6                   /* keep dx, dy */
        move.w  d1,d7
        /* d2 = |dx|^2 + |dy|^2 */
        move.w  d0,d2
        bpl   1f
        neg.w   d2
1:      mulu    d2,d2
        move.w  d1,d3
        bpl   2f
        neg.w   d3
2:      mulu    d3,d3
        add.l   d3,d2
        /* zone */
        moveq   #0,d4
        move.b  C21D,d4
        mulu    #12,d4
        lea     T_ZONA(a6),a0
        adda.w  d4,a0
        moveq   #0,d5                   /* z */
3:      cmp.l   (a0)+,d2
        bcs   4f
        addq.w  #1,d5
        cmpi.w  #3,d5
        bcs   3b
4:      /* dx "== 0" test: (dxH + dxL) & 0xFF == 0 */
        move.w  d6,d0
        move.b  d0,d1
        lsr.w   #8,d0
        add.b   d1,d0
        beq     ap_ret
        /* quadrant */
        move.w  d6,d0
        move.w  d7,d1
        tst.w   d0
        bmi   5f
        tst.w   d1
        bmi   6f
        lea     T_ATAN_PP(a6),a0
        bra   7f
6:      neg.w   d1
        lea     T_ATAN_PN(a6),a0
        bra   7f
5:      neg.w   d0
        tst.w   d1
        bmi   8f
        lea     T_ATAN_NP(a6),a0
        bra   7f
8:      neg.w   d1
        lea     T_ATAN_NN(a6),a0
7:      /* q = (dy << 8) / dx, 16-step restoring division */
        move.l  d1,d2
        and.l   #0xFFFF,d2
        lsl.l   #8,d2
        move.w  d0,d3
        bsr     div32x16
        and.l   #0xFFFF,d2              /* q */
        /* find the first entry with q <= ratio (11 entries of 4 bytes), else the last (MCU 0x0400: an
           equal ratio selects the entry) */
        moveq   #10,d3
9:      cmp.w   (a0),d2
        bls   10f
        addq.l  #4,a0
        dbra    d3,9b
        subq.l  #4,a0                   /* none: last entry */
10:     moveq   #0,d0
        tst.b   C215
        bne   11f
        move.b  2(a0),d0                /* obj = e.a */
        lea     zone_adj0(pc),a2
        add.b   (a2,d5.w),d0
        bsr     mod40
        sub.b   EAB1,d0                 /* pasos = obj - cur */
        bsr     mod40
        move.b  C1FC,d1
        and.b   #2,d1
        bra   12f
11:     move.b  3(a0),d0                /* obj = e.b */
        lea     zone_adj1(pc),a2
        add.b   (a2,d5.w),d0
        bsr     mod40
        move.b  EAB1,d1
        sub.b   d0,d1                   /* pasos = cur - obj */
        move.b  d1,d0
        bsr     mod40
        move.b  C1FC,d1
        and.b   #1,d1
12:     /* d0 = pasos, d1 = bit */
        tst.b   d0
        beq   13f
        cmpi.b  #20,d0
        bcc   13f
        move.b  C20F,C20D
        tst.b   d1
        beq   ap_ret
        move.b  #2,C20D
        move.b  #2,C22B
        rts
13:     clr.b   C20D
        move.b  #1,C22B
        tst.b   d1
        beq   ap_ret
        move.b  #3,C20D
        move.b  #2,C22B
ap_ret:
        rts
zone_adj0:
        .byte   -2,-1,1,2
zone_adj1:
        .byte   2,1,-1,-2

* mod40: d0.b -> d0.b mod 40 (the MCU adjusts by repeated +/-40)
mod40:
        tst.b   d0
        bpl   1f
        add.b   #40,d0
        bra   mod40
1:      cmpi.b  #40,d0
        bcs   2f
        sub.b   #40,d0
        bra   1b
2:      rts

ap_state2:
        tst.b   EAD1
        bne   1f
        clr.b   C229
        clr.b   C22D
        move.b  C22F,C233
        rts
1:      move.b  C22F,d0                 /* obj */
        move.b  EAB1,d1                 /* cur */
        cmpi.b  #1,C215
        beq   2f
        sub.b   d1,d0                   /* pasos = obj - cur */
        bsr     mod40
        move.b  C1FC,d1
        and.b   #1,d1
        bra   3f
2:      sub.b   d0,d1                   /* pasos = cur - obj */
        move.b  d1,d0
        bsr     mod40
        move.b  C1FC,d1
        and.b   #2,d1
3:      tst.b   d0
        beq   4f
        cmpi.b  #20,d0
        bcc   4f
        move.b  #0x0A,C20D
        tst.b   d1
        bne   ap_ret2
        move.b  #1,C20D
        rts
4:      tst.b   d1
        bne   5f
        move.b  #3,C20D
        rts
5:      cmpi.b  #1,C231
        bne   6f
        move.b  #1,C22D
        rts
6:      clr.b   C20D
        move.b  #1,C22B
        move.b  C231,d0
        bne   7f
        moveq   #0x15,d0
7:      subq.b  #1,d0
        move.b  d0,C231
        moveq   #0,d0
        move.b  C22F,d0
        move.b  d0,EAB1
        mulu    #30,d0
        move.w  d0,EABC
        bsr     velocidad
ap_ret2:
        rts

* velocidad (MCU 0x0618): EAA8 = ext(sin[EAB1]*EAD2), EAAC = ext(cos[EAB1]*EAD2)
*   P = |s| * v, negated when s < 0; ext(P) = (P >> 8) << 1 | cin, top byte 0xFF if bit 31 of P was set,
*   else 0.  cin is the carry the MCU leaves before its shift (an RL, which keeps the carry): 1 from the
*   CJNE of the positive case, the borrow of the negation in the negative case (1 unless P = 0)
velocidad:
        moveq   #0,d0
        move.b  EAB1,d0
        lsl.w   #2,d0
        lea     T_SIN(a6),a0
        adda.w  d0,a0
        moveq   #0,d4
        move.w  EAD2,d4
        move.w  (a0)+,d1
        bsr     vel_one
        move.l  d0,EAA8
        move.w  (a0),d1
        bsr     vel_one
        move.l  d0,EAAC
        rts
* vel_one: d1.w = table value (signed), d4 = speed -> d0.l.  Trashes d1-d2.
vel_one:
        move.w  d1,d2
        bpl   1f
        neg.w   d2
1:      and.l   #0xFFFF,d2
        mulu    d4,d2                   /* |s| * v, 32 bits */
        tst.w   d1
        smi     d1                      /* d1.b = 0xFF: negative case */
        bpl   2f
        neg.l   d2                      /* P (X = borrow = P != 0) */
2:      move.l  d2,d0
        clr.b   d0
        lsr.l   #7,d0
        and.l   #0x00FFFFFF,d0          /* (P >> 8) << 1 */
        tst.b   d1
        beq   4f                        /* positive: cin = 1 */
        tst.l   d2
        beq   5f                        /* negative zero: no borrow, cin = 0 */
4:      or.b    #1,d0
5:      tst.l   d2
        bpl   3f
        or.l    #0xFF000000,d0
3:      rts

* ------------------------------------------------------------------------------------------
* wrdallas.bin image (tables + scripts), 0x0000-0x4090
* ------------------------------------------------------------------------------------------
        .align  2
mcu_rom:
        .incbin "wrdallas.bin",0,0x4090
        .align  2
