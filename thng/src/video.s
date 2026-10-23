* video.s - the arcade picture rebuilt with Neo Geo sprites, once per frame from the VBlank handler.
*
* Tilemaps (2 x 32x32 cells of 16x16, 512x512 px, wrap-around scroll): NCOL column sprites each, 32 tiles
* tall (a 512-line sprite wraps around the sprite space: vertical scroll = sprite Y).  Map column c lives
* in column sprite c mod 21: the 21 columns on screen are always 21 consecutive map columns, so scrolling
* by 16 px reloads one column sprite.  The crypt stubs mark the map columns the game writes (vmark).
* Arcade sprites (511 entries of 4 words at 440006 + 8 n, entry 0 on top): one Neo Geo sprite each,
* 16x16 = arcade tile code >> 2, 8x8 = tile padded in a 16x16 one (gfx2crom.py map8).
* The text (8x8 font, codes 0xF400-0xFFFF, colours 0x30-0x3F, always on the 8-pixel grid) goes to the
* fix layer instead (fix tile = code - 0xF000, palette = colour - 0x30): forty 8x8 characters on a line
* would be forty 16-line Neo Geo sprites, past the 96 per scanline with the 42 tilemap columns.
* Order (= priority): tilemap 1 < tilemap 0 < sprites < text.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global video_init_min, video_init, video_vblank

video_init_min:
        move.w  #0x4000,REG_LSPCMODE
        move.b  d0,REG_NOSHADOW
        move.b  d0,REG_CRTFIX
        move.b  d0,REG_PALBANK0
        move.w  #VRAM_SCB2,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        move.w  #511,d0
1:      move.w  #0x0FFF,REG_VRAMRW
        dbra    d0,1b
        VRAM_GAP
        move.w  #VRAM_SCB3,REG_VRAMADDR
        move.w  #511,d0
2:      move.w  #0,REG_VRAMRW
        dbra    d0,2b
        VRAM_GAP
        move.w  #VRAM_FIX,REG_VRAMADDR
        move.w  #40*32-1,d0
3:      move.w  #0x00FF,REG_VRAMRW
        dbra    d0,3b
        lea     PALETTES+PAL_FIX*32,a0
        move.w  #0x8000,(a0)+
        move.w  #0x7FFF,(a0)+
        move.w  #0x7FF0,(a0)+
        move.w  #0x8000,(a0)+
        moveq   #11,d0
4:      move.w  #0x4210,(a0)+
        dbra    d0,4b
        move.w  #0x8000,PALETTES+255*32+30
        rts

video_init:
        bsr     video_init_min
        /* arcade palettes black (the shadow is 0 = black too) */
        lea     PALETTES+PAL_ARC*32,a0
        move.w  #1023,d0
1:      move.w  #0x8000,(a0)+
        dbra    d0,1b
        /* the column sprites: 32 tiles tall, reloaded on the first frame */
        lea     rt_slotcol,a0
        moveq   #2*NCOL-1,d0
2:      st      (a0)+
        dbra    d0,2b
        /* sprite entries: all unused, no text on the fix layer */
        lea     ENT_SH,a0
        move.w  #510,d0
3:      move.w  #0x00F0,(a0)+
        clr.l   (a0)+
        dbra    d0,3b
        lea     ENT_FIX,a0
        move.w  #510,d0
4:      clr.w   (a0)+
        dbra    d0,4b
        bsr     fix_clear_stamps
        rts

fix_clear_stamps:
        lea     FIX_STAMP,a0
        move.w  #40*32/4-1,d0
1:      clr.l   (a0)+
        dbra    d0,1b
        move.b  #1,rt_fid
        rts

* ------------------------------------------------------------------------------------------
video_vblank:
        bsr     pal_sync
        move.w  REG_LSPCMODE,rt_prof+6
        lea     ARC_VRAM+0x1000,a4      /* tilemap 1: vregs 2 (y), 3 (x) */
        move.w  ARC_VREGS+4,d4
        move.w  ARC_VREGS+6,d5
        move.w  #SPR_TM1,d6
        lea     rt_slotcol+NCOL,a5
        lea     rt_dirty1,a6
        bsr     layer_sync
        lea     ARC_VRAM,a4             /* tilemap 0: vregs 0 (y), 1 (x) + 4 */
        move.w  ARC_VREGS+0,d4
        move.w  ARC_VREGS+2,d5
        addq.w  #4,d5
        move.w  #SPR_TM0,d6
        lea     rt_slotcol,a5
        lea     rt_dirty0,a6
        bsr     layer_sync
        move.w  REG_LSPCMODE,rt_prof+8
        bra     spr_sync

* ------------------------------------------------------------------------------------------
* pal_sync: arcade palette RAM (xBGR555) -> Neo Geo palettes PAL_ARC.., only the colours that changed
* (compared two at a time with the shadow of the last converted values).  Backdrop = arcade colour 0.
pal_sync:
        lea     ARC_PAL,a0
        lea     PAL_SHADOW,a1
        lea     CT_PAL,a3
        move.w  #511,d7
1:      cmpm.l  (a0)+,(a1)+
        bne.s   3f
2:      dbra    d7,1b
        move.w  PALETTES+PAL_ARC*32,PALETTES+255*32+30
        rts
3:      move.l  -4(a0),d0
        move.l  d0,-4(a1)
        move.l  a0,d1
        subi.l  #ARC_PAL+4,d1           /* byte offset of the pair */
        lea     PALETTES+PAL_ARC*32,a2
        adda.w  d1,a2
        swap    d0
        andi.l  #0x7FFF,d0
        add.w   d0,d0
        move.w  (a3,d0.l),(a2)+
        move.l  -4(a0),d0
        andi.l  #0x7FFF,d0
        add.w   d0,d0
        move.w  (a3,d0.l),(a2)
        cmpi.w  #PAL_TEXT*32,d1         /* arcade palettes 48-63: also the fix palettes 0-15 */
        bcs.s   2b
        move.l  -2(a2),-2-(PAL_TEXT+PAL_ARC)*32(a2)
        bra.s   2b

* ------------------------------------------------------------------------------------------
* layer_sync: a4 = map, d4 = scroll y, d5 = scroll x, d6 = first column sprite, a5 = its 21 slot
* bytes, a6 = dirty mask of the map columns
layer_sync:
        lea     mod21(pc),a1
        move.w  d5,d0
        lsr.w   #4,d0
        andi.w  #31,d0                  /* d0 = first map column on screen */
        move.l  (a6),d3                 /* dirty columns */
        clr.l   (a6)
        moveq   #0,d7                   /* j */
1:      move.w  d0,d1
        add.w   d7,d1
        andi.w  #31,d1                  /* c */
        moveq   #0,d2
        move.b  (a1,d1.w),d2       /* k */
        cmp.b   (a5,d2.w),d1
        bne.s   2f
        btst    d1,d3
        beq.s   3f
2:      move.b  d1,(a5,d2.w)
        movem.l d0/d3-d7,-(sp)
        bsr     col_upload
        movem.l (sp)+,d0/d3-d7
3:      addq.w  #1,d7
        cmpi.w  #NCOL,d7
        bne.s   1b
        /* SCB3: every column sprite y | 32 tiles; SCB4: x of the sprite holding map column c */
        move.w  #VRAM_SCB3,d1
        add.w   d6,d1
        move.w  d1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        move.w  d4,d1
        addi.w  #496+CROP_Y,d1          /* 496 - (-y - CROP_Y) */
        andi.w  #0x1FF,d1
        lsl.w   #7,d1
        ori.w   #32,d1
        moveq   #NCOL-1,d2
4:      move.w  d1,REG_VRAMRW
        dbra    d2,4b
        /* x: for k = 0..20, the column on screen j with (first + j) mod 21 == k... computed by
           walking j and writing at the sprite's own address */
        moveq   #15,d2
        and.w   d5,d2                   /* fine scroll */
        neg.w   d2
        moveq   #0,d7
5:      move.w  d0,d1
        add.w   d7,d1
        andi.w  #31,d1
        moveq   #0,d3
        move.b  (a1,d1.w),d3
        add.w   d6,d3
        addi.w  #VRAM_SCB4,d3
        VRAM_GAP
        move.w  d3,REG_VRAMADDR
        move.w  d2,d1
        andi.w  #0x1FF,d1
        lsl.w   #7,d1
        move.w  d1,REG_VRAMRW
        addi.w  #16,d2
        addq.w  #1,d7
        cmpi.w  #NCOL,d7
        bne.s   5b
        rts

mod21:
        .byte   0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20
        .byte   0,1,2,3,4,5,6,7,8,9,10
        .align  2

* col_upload: map column d1 of map a4 -> column sprite d6 + d2
col_upload:
        add.w   d6,d2
        lsl.w   #6,d2
        VRAM_GAP
        move.w  d2,REG_VRAMADDR         /* SCB1 of the sprite */
        move.w  #1,REG_VRAMMOD
        lsl.w   #2,d1
        lea     (a4,d1.w),a0
        moveq   #31,d7
1:      move.w  (a0),d0
        move.w  d0,d1
        lsr.w   #2,d0
        ori.w   #0x4000,d0              /* tilemap tiles: Neo Geo tiles 0x4000 + code */
        move.w  d0,REG_VRAMRW
        move.w  2(a0),d2
        andi.w  #0x3F,d2
        addi.w  #PAL_ARC,d2
        lsl.w   #8,d2
        andi.w  #3,d1                   /* flip x / y = Neo Geo bits 0 / 1 */
        or.w    d1,d2
        move.w  d2,REG_VRAMRW
        lea     128(a0),a0
        dbra    d7,1b
        rts

* ------------------------------------------------------------------------------------------
* spr_sync: arcade sprite list -> Neo Geo sprites.  Entry k (0..NSLOT-1) always uses Neo Geo sprite
* SPR_MAX-1-k (entry 0 on top, as on the arcade), so only the entries that changed since last frame
* (ENT_SH = their words then) are rewritten.  Unused entries have y = 0xF0: height 0.  Text characters go
* to the fix layer (ENT_FIX = where the entry's character is); cells a character left are blanked at the
* end, unless text came there this frame.  Entries from NSLOT on can only be text.
spr_sync:
        tst.b   rt_sprdirty
        beq     32f
        clr.b   rt_sprdirty
        /* entries 0..n-1, n = max(used now (FFD2D8), scanned last time): the game sets the ones it no
           longer uses to y = 0xF0 */
        move.w  ARC_WRAM+0xD2D8-0x8000,d7
        cmpi.w  #511,d7
        bls.s   1f
        move.w  #511,d7
1:      move.w  rt_sprprev,d0
        move.w  d7,rt_sprprev
        cmp.w   d0,d7
        bcc.s   2f
        move.w  d0,d7
2:      tst.w   d7
        beq     32f
        move.w  d7,d0
        lsl.w   #3,d0
        lea     ARC_SPR+6,a0
        adda.w  d0,a0
        move.w  d7,d0
        add.w   d0,d0
        lea     ENT_FIX,a6
        adda.w  d0,a6
        move.w  d0,d1
        add.w   d0,d0
        add.w   d1,d0
        lea     ENT_SH,a1
        adda.w  d0,a1
        lea     FIX_CLRQ,a5
        move.w  #SPR_MAX,d6
        sub.w   d7,d6                   /* Neo Geo sprite of entry k = SPR_MAX-1-k (< SPR_ARC: none) */
        subq.w  #1,d7
0:      subq.l  #8,a0
        subq.l  #6,a1
        subq.l  #2,a6
        move.w  (a0),d0
        cmp.w   (a1),d0
        bne.s   2f
        move.w  4(a0),d2
        cmp.w   2(a1),d2
        bne.s   2f
        move.w  6(a0),d4
        cmp.w   4(a1),d4
        beq     8f                      /* unchanged */
2:      move.w  d0,(a1)
        move.w  4(a0),2(a1)
        move.w  6(a0),4(a1)
        tst.w   (a6)
        beq.s   3f
        move.w  (a6),(a5)+              /* it had a character there */
        clr.w   (a6)
3:      cmpi.b  #0xF0,d0
        beq     40f                     /* unused */
        /* text: 8x8, no flip, code >= 0xF400, colour >= 0x30, on the fix grid */
        andi.w  #0xC800,d0
        cmpi.w  #0x0800,d0
        bne     20f
        move.w  6(a0),d4
        cmpi.w  #0xF400,d4
        bcs     20f
        move.w  4(a0),d2
        move.w  d2,d0
        andi.w  #0x6000,d0
        cmpi.w  #0x6000,d0
        bne     20f
        move.w  d2,d0
        andi.w  #0x1FF,d0
        add.w   d0,d0
        lea     fix_xt,a4
        move.w  (a4,d0.w),d3            /* column * 32, or < 0 off the grid */
        bmi     20f
        moveq   #0,d0
        move.b  1(a0),d0
        add.w   d0,d0
        lea     fix_yt,a4
        move.w  (a4,d0.w),d0            /* row, or < 0 */
        bmi     20f
        add.w   d0,d3
        addi.w  #VRAM_FIX+2,d3          /* fix rows 2-29 are the visible 224 lines */
        lsl.w   #3,d2
        andi.w  #0xF000,d2
        andi.w  #0x0FFF,d4
        or.w    d2,d4                   /* fix word: palette = colour & 15 */
        VRAM_GAP
        move.w  d3,REG_VRAMADDR
        move.w  d4,REG_VRAMRW
        move.w  d3,(a6)
        lea     FIX_STAMP-VRAM_FIX,a4
        move.b  rt_fid,(a4,d3.w)
40:     /* no sprite for this entry: height 0 */
        cmpi.w  #SPR_ARC,d6
        blt     8f
        move.w  #VRAM_SCB3,d0
        add.w   d6,d0
        VRAM_GAP
        move.w  d0,REG_VRAMADDR
        move.w  #0,REG_VRAMRW
        bra     8f
20:     /* a sprite: d1 = sy */
        cmpi.w  #SPR_ARC,d6
        blt     8f                      /* past the Neo Geo sprites */
        moveq   #0,d1
        move.b  1(a0),d1
        neg.w   d1
        addi.w  #240,d1
        andi.w  #0xFF,d1
        cmpi.w  #CROP_Y+224,d1
        bcc     40b
        cmpi.w  #CROP_Y-16,d1
        bls     40b
        move.w  4(a0),d2                /* colour | x */
        move.w  d2,d3
        andi.w  #0x1FF,d3
        subi.w  #15,d3                  /* screen x */
        moveq   #0,d4
        move.w  6(a0),d4                /* code */
        move.w  (a0),d0
        moveq   #0,d5                   /* flips */
        btst    #14,d0
        beq.s   21f
        moveq   #1,d5
21:     btst    #15,d0
        beq.s   22f
        addq.w  #2,d5
22:     btst    #11,d0
        bne.s   23f
        lsr.w   #2,d4                   /* 16x16: Neo Geo tile = code >> 2 */
        bra.s   25f
23:     add.l   d4,d4                   /* 8x8: map8 */
        lea     CT_MAP8,a4
        move.w  (a4,d4.l),d4
        andi.l  #0xFFFF,d4
        cmpi.w  #0xFFFF,d4
        beq     40b
        addi.l  #0x8000,d4
        btst    #0,d5
        beq.s   24f
        subq.w  #8,d3
24:     btst    #1,d5
        beq.s   25f
        subq.w  #8,d1
25:     /* SCB1 */
        move.w  d6,d0
        lsl.w   #6,d0
        VRAM_GAP
        move.w  d0,REG_VRAMADDR
        move.w  d4,REG_VRAMRW
        swap    d4
        lsl.w   #4,d4                   /* tile bits 16-19 */
        or.w    d4,d5
        lsr.w   #8,d2
        lsr.w   #1,d2
        andi.w  #0x3F,d2
        addi.w  #PAL_ARC,d2
        lsl.w   #8,d2
        or.w    d2,d5
        move.w  d5,REG_VRAMRW
        /* SCB3: y | 1 tile; SCB4: x */
        move.w  #CROP_Y+496,d0
        sub.w   d1,d0
        andi.w  #0x1FF,d0
        lsl.w   #7,d0
        addq.w  #1,d0
        move.w  #VRAM_SCB3,d1
        add.w   d6,d1
        VRAM_GAP
        move.w  d1,REG_VRAMADDR
        move.w  d0,REG_VRAMRW
        andi.w  #0x1FF,d3
        lsl.w   #7,d3
        addi.w  #VRAM_SCB4-VRAM_SCB3,d1
        VRAM_GAP
        move.w  d1,REG_VRAMADDR
        move.w  d3,REG_VRAMRW
8:      addq.w  #1,d6                   /* next entry: one sprite up */
        dbra    d7,0b
        /* blank the cells characters left (unless text came there this frame); next frame id */
        move.b  rt_fid,d1
        lea     FIX_STAMP-VRAM_FIX,a4
        lea     FIX_CLRQ,a1
        bra.s   31f
30:     move.w  (a1)+,d0
        cmp.b   (a4,d0.w),d1
        beq.s   31f
        VRAM_GAP
        move.w  d0,REG_VRAMADDR
        move.w  #FIX_BLANK,REG_VRAMRW
31:     cmpa.l  a5,a1
        bcs.s   30b
        addq.b  #1,rt_fid
        bne.s   32f
        bsr     fix_clear_stamps
32:     rts

* fix_xt: arcade x (9 bits) -> column * 32 (fix VRAM is column major), -1 off the grid
fix_xt:
        .set    i,0
        .rept   512
        .if     ((i-15)&7) == 0 && i >= 15 && i < 15+320
        .word   ((i-15)/8)*32
        .else
        .word   -1
        .endif
        .set    i,i+1
        .endr
* fix_yt: arcade y byte -> row, -1 off the grid (sy = (240 - y) & 0xFF, rows from CROP_Y)
fix_yt:
        .set    i,0
        .rept   256
        .set    sy,(240-i)&0xFF
        .if     ((sy-CROP_Y)&7) == 0 && sy >= CROP_Y && sy < CROP_Y+224
        .word   (sy-CROP_Y)/8
        .else
        .word   -1
        .endif
        .set    i,i+1
        .endr

