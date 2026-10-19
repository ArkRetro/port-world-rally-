* eye.s - the start-up screen in the Neo Geo "eye-catcher" slot (header 0x114 = 1: the BIOS calls USER with
* request 1 and the game draws it): a metallic GAELCO logo opens from the centre (horizontal shrink of a
* chain of column sprites, 0 -> full) while it fades in, then "MAX 330 MEGA" and "PRO-BLOSTE SPEC" light
* up below it; after about 3 s everything fades out.  Graphics and palettes: tools/mklogo.py (logo.inc,
* tiles at LOGO_TILE.. placed by gfx2crom --extra).  Runs with the interrupts off: it waits for the
* vertical blank on the raster counter and kicks the watchdog itself.

        .include "neogeo.i"
        .include "ram.i"

        .section .rodata
        .include "logo.inc"             /* first: its sizes are used in expressions below */
        .text

        .global eye_main

        .equ    EYE_LOGO, 1                     /* sprites: the logo columns, then the two lines of text */
        .equ    EYE_MAX,  EYE_LOGO+LOGO_COLS
        .equ    EYE_SPEC, EYE_MAX+MAXT_COLS
        .equ    EYE_END,  EYE_SPEC+SPEC_COLS
        .equ    PAL_EYE_LOGO, 2
        .equ    PAL_EYE_TEXT, 3
        /* screen rows (0 = top visible line): the block (64 + 18 + 32 + 2 + 32 lines) centred in 224 */
        .equ    Y_LOGO, 38
        .equ    Y_MAX,  120
        .equ    Y_SPEC, 154
        .equ    T_LOGO, 0                       /* frame the logo starts opening (32 frames) */
        .equ    T_MAX,  40                      /* frames the lines start fading in (16 frames) */
        .equ    T_SPEC, 56
        .equ    T_OUT,  200                     /* fade out (16 frames), then back to the BIOS */
        .equ    T_END,  T_OUT+16

        .macro  SCB3Y yline, rows
        .word   (((0x200 - 16 - \yline) & 0x1FF) << 7) + \rows
        .endm

eye_main:
        move.l  #EYE_MAGIC,EYE_SHOWN
        move.w  sr,-(sp)
        ori.w   #0x0700,sr                      /* no interrupts (the BIOS's request 1 has them off too) */
        bsr     video_init_min                  /* sprites off, fix cleared, black backdrop */
        moveq   #0,d0
        bsr     eye_pal_logo
        moveq   #0,d0
        bsr     eye_pal_text
        /* SCB1 of every sprite: its column of tiles */
        move.w  #1,REG_VRAMMOD
        move.w  #EYE_LOGO,d0
        move.l  #LOGO_TILE,d1
        moveq   #LOGO_COLS-1,d2
        moveq   #LOGO_ROWS,d3
        move.w  #PAL_EYE_LOGO<<8,d4
        bsr     eye_scb1
        move.w  #EYE_MAX,d0
        move.l  #MAXT_TILE,d1
        moveq   #MAXT_COLS-1,d2
        moveq   #MAXT_ROWS,d3
        move.w  #PAL_EYE_TEXT<<8,d4
        bsr     eye_scb1
        move.w  #EYE_SPEC,d0
        move.l  #SPEC_TILE,d1
        moveq   #SPEC_COLS-1,d2
        moveq   #SPEC_ROWS,d3
        move.w  #PAL_EYE_TEXT<<8,d4
        bsr     eye_scb1
        /* the two lines: full width, centred, shown from the start (black until their palette fades in) */
        move.w  #EYE_MAX,d0
        moveq   #MAXT_COLS,d1
        move.w  scb3_max(pc),d2
        move.w  #(320-MAXT_COLS*16)/2,d3
        moveq   #15,d4
        bsr     eye_chain
        move.w  #EYE_SPEC,d0
        moveq   #SPEC_COLS,d1
        move.w  scb3_spec(pc),d2
        move.w  #(320-SPEC_COLS*16)/2,d3
        moveq   #15,d4
        bsr     eye_chain

        moveq   #0,d7                           /* frame */
eye_loop:
        bsr     eye_wait_vbl
        /* the logo: width (hs + 1) per column, hs = 0..15 over 32 frames, centred */
        move.w  d7,d4
        subi.w  #T_LOGO,d4
        bmi.s   1f
        lsr.w   #1,d4
        cmpi.w  #15,d4
        bls.s   2f
        moveq   #15,d4
2:      move.w  #EYE_LOGO,d0
        moveq   #LOGO_COLS,d1
        move.w  scb3_logo(pc),d2
        move.w  d4,d3
        addq.w  #1,d3
        mulu    #LOGO_COLS,d3
        lsr.w   #1,d3
        neg.w   d3
        addi.w  #160,d3                         /* x = 160 - width / 2 */
        bsr     eye_chain
1:      /* palettes: logo fading in with its opening, the lines after it, all fading out at the end */
        move.w  d7,d0
        subi.w  #T_LOGO,d0
        lsr.w   #1,d0
        bsr     eye_level
        bsr     eye_pal_logo
        move.w  d7,d0
        subi.w  #T_MAX,d0
        bsr     eye_level
        bsr     eye_pal_text
        addq.w  #1,d7
        cmpi.w  #T_END,d7
        bcs     eye_loop
        /* hide the sprites again for the BIOS */
        bsr     eye_wait_vbl
        move.w  #1,REG_VRAMMOD
        move.w  #VRAM_SCB3+EYE_LOGO,REG_VRAMADDR
        move.w  #EYE_END-EYE_LOGO-1,d0
3:      move.w  #0,REG_VRAMRW
        nop
        dbra    d0,3b
        move.w  (sp)+,sr
        rts

* eye_level: d0 = frames since the fade-in began (negative: not yet) -> d0 = palette step 0..15, ramped
* down again over the last 16 frames (T_OUT..T_END).  Uses d7 (frame).
eye_level:
        tst.w   d0
        bpl.s   1f
        moveq   #0,d0
1:      cmpi.w  #15,d0
        bls.s   2f
        moveq   #15,d0
2:      cmpi.w  #T_OUT,d7
        bcs.s   3f
        move.w  #T_END-1,d1
        sub.w   d7,d1                           /* 15 .. 0 */
        cmp.w   d1,d0
        bls.s   3f
        move.w  d1,d0
3:      rts

* eye_pal_logo / eye_pal_text: d0 = step 0..15 -> the palette (16 colours of that step)
eye_pal_logo:
        lea     pal_logo,a0
        lea     PALETTES+PAL_EYE_LOGO*32,a1
        bra.s   1f
eye_pal_text:
        lea     pal_text,a0
        lea     PALETTES+PAL_EYE_TEXT*32,a1
1:      lsl.w   #5,d0
        adda.w  d0,a0
        moveq   #7,d0
2:      move.l  (a0)+,(a1)+
        dbra    d0,2b
        rts

* eye_scb1: d0 = first sprite, d1.l = first tile, d2 = columns - 1, d3 = rows, d4 = attribute.  Tiles column
* by column (mklogo.py order).
eye_scb1:
        move.w  d0,d5
        lsl.w   #6,d5
        move.w  d5,REG_VRAMADDR
        move.w  d3,d6
        subq.w  #1,d6
1:      move.w  d1,REG_VRAMRW
        addq.l  #1,d1
        nop
        move.w  d4,REG_VRAMRW
        nop
        dbra    d6,1b
        addq.w  #1,d0
        dbra    d2,eye_scb1
        rts

* eye_chain: d0 = first sprite, d1 = columns, d2 = SCB3 of the first (y | rows), d3 = x, d4 = horizontal
* shrink 0..15.  The others are chained to it (sticky): each one hs + 1 pixels right of the previous.
eye_chain:
        move.w  d4,d5
        lsl.w   #8,d5
        ori.w   #0xFF,d5                        /* SCB2: hs, no vertical shrink */
        move.w  #1,REG_VRAMMOD
        move.w  d0,d6
        add.w   #VRAM_SCB2,d6
        move.w  d6,REG_VRAMADDR
        move.w  d1,d6
        subq.w  #1,d6
1:      move.w  d5,REG_VRAMRW
        nop
        dbra    d6,1b
        move.w  d0,d6
        add.w   #VRAM_SCB3,d6
        move.w  d6,REG_VRAMADDR
        move.w  d2,REG_VRAMRW
        nop
        move.w  d1,d6
        subq.w  #2,d6
        bmi.s   3f
2:      move.w  #0x40,REG_VRAMRW                /* sticky */
        nop
        dbra    d6,2b
3:      move.w  d0,d6
        add.w   #VRAM_SCB4,d6
        move.w  d6,REG_VRAMADDR
        lsl.w   #7,d3
        move.w  d3,REG_VRAMRW
        rts

* eye_wait_vbl: kick the watchdog, wait for the start of the vertical blank (raster line 0x1F0)
eye_wait_vbl:
        move.b  d0,REG_WATCHDOG
1:      move.w  REG_LSPCMODE,d0                 /* out of the blank first (not twice in the same one) */
        lsr.w   #7,d0
        cmpi.w  #0x1F0,d0
        bcc.s   1b
2:      move.w  REG_LSPCMODE,d0
        lsr.w   #7,d0
        cmpi.w  #0x1F0,d0
        bcs.s   2b
        rts

scb3_logo:
        SCB3Y   Y_LOGO, LOGO_ROWS
scb3_max:
        SCB3Y   Y_MAX, MAXT_ROWS
scb3_spec:
        SCB3Y   Y_SPEC, SPEC_ROWS
