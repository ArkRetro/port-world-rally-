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
        /* sprite 0: the LSPC draws it at the end of every scanline list with fewer than 96 sprites (real
           hardware; MAME does not): transparent tiles, off screen */
        move.w  #0,REG_VRAMADDR
        moveq   #31,d0
6:      move.w  #HI_BLANK&0xFFFF,REG_VRAMRW
        VRAM_GAP
        move.w  #(HI_BLANK>>16)<<4,REG_VRAMRW
        VRAM_GAP
        dbra    d0,6b
        move.w  #VRAM_SCB4,REG_VRAMADDR
        VRAM_GAP
        move.w  #320<<7,REG_VRAMRW
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
        lea     PAL_SHADOW,a0           /* and the shadow of the converted values black */
        move.w  #511,d0
3:      clr.l   (a0)+
        dbra    d0,3b
        /* sentinel: the MVS BIOS clears the palette RAM when a coin goes in (main.s rebuilds then) */
        move.w  #0x0123,PALETTES+254*32+2
        st      rt_sprdirty
        /* the column sprites: 32 tiles tall, reloaded on the first frame */
        lea     rt_slotcol,a0
        moveq   #2*NCOL-1,d0
2:      st      (a0)+
        dbra    d0,2b
        lea     RT_COLSLOT,a0
        moveq   #63,d0
5:      st      (a0)+
        dbra    d0,5b
        move.w  #CROP_Y,rt_pcrop
        /* sprite entries: all unused, no text on the fix layer */
        lea     ENT_SH,a0
        move.w  #510,d0
3:      move.w  #0x00F0,(a0)+
        clr.w   (a0)+
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
        /* progressive frameskip: no new game frame since the last VBlank (rt_newframe) means the game
           is behind: level up (max 3), else down.  At level n the layers and sprites are converted once
           every n+1 frames (scroll, columns and sprite changes wait in their dirty marks): the CPU time
           goes to the game */
        move.b  rt_fskip,d0
        tst.b   rt_gframe
        bne.s   1f
        cmpi.b  #3,d0
        bcc.s   2f
        addq.b  #1,d0
        bra.s   2f
1:      clr.b   rt_gframe
        tst.b   d0
        beq.s   2f
        subq.b  #1,d0
2:      move.b  d0,rt_fskip
        beq.s   3f
        cmp.b   rt_fsk_n,d0
        bls.s   3f
        addq.b  #1,rt_fsk_n
        rts                             /* skipped */
3:      clr.b   rt_fsk_n
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
        /* palettes to look at: all 64 after the game loaded the palette base (rt_pal_an) and every 64
           frames for safety, otherwise the ones its copy / load / fade routines marked */
        tst.b   rt_paldirty
        bne.s   0f
        moveq   #63,d0
        and.w   rt_frame+2,d0
        bne.s   20f
0:      clr.b   rt_paldirty
        lea     RT_PALMARK,a0
        moveq   #-1,d0
        moveq   #15,d1
21:     move.l  d0,(a0)+
        dbra    d1,21b
20:     /* marked palettes: compare their 8 longs with the shadow, convert the pairs that changed.  The
           palette RAM is touched only in the VBlank: past it, the marks left wait for the next one */
        lea     RT_PALMARK,a2
        lea     CT_PAL,a3
        moveq   #0,d6                   /* palette */
22:     tst.b   (a2,d6.w)
        beq     24f
        PAL_LATE 9f
        clr.b   (a2,d6.w)
        move.w  d6,d0
        lsl.w   #5,d0
        lea     ARC_PAL,a0
        adda.w  d0,a0
        lea     PAL_SHADOW,a1
        adda.w  d0,a1
        lea     PALETTES+PAL_ARC*32,a4
        adda.w  d0,a4
        moveq   #7,d5
23:     move.l  (a0)+,d0
        cmp.l   (a1)+,d0
        beq.s   25f
        move.l  d0,-4(a1)
        move.l  d0,d1
        swap    d0
        andi.l  #0x7FFF,d0
        add.w   d0,d0
        move.w  (a3,d0.l),(a4)+
        andi.l  #0x7FFF,d1
        add.w   d1,d1
        move.w  (a3,d1.l),(a4)+
        dbra    d5,23b
        bra.s   26f
25:     addq.l  #4,a4
        dbra    d5,23b
26:     cmpi.w  #PAL_TEXT,d6            /* arcade palettes 48-63: also the fix palettes 0-15 */
        bcs.s   24f
        lea     -32-(PAL_TEXT+PAL_ARC)*32(a4),a0
        lea     -32(a4),a1
        moveq   #7,d5
27:     move.l  (a1)+,(a0)+
        dbra    d5,27b
24:     addq.w  #1,d6
        cmpi.w  #64,d6
        bne     22b
        PAL_LATE 9f
        move.w  PALETTES+PAL_ARC*32,PALETTES+255*32+30
9:      rts

* ------------------------------------------------------------------------------------------
* layer_sync: a4 = map, d4 = scroll y, d5 = scroll x, d6 = first column sprite, a5 = its 21 slot
* bytes, a6 = dirty mask of the map columns
layer_sync:
        /* nothing to do if no column was written and the scroll and crop are the ones last written */
        tst.l   (a6)
        bne.s   1f
        lea     rt_lcache,a0
        cmpi.w  #SPR_TM0,d6
        bne.s   2f
        addq.l  #8,a0
2:      cmp.w   (a0),d5
        bne.s   1f
        cmp.w   2(a0),d4
        bne.s   1f
        move.w  rt_pcrop,d1
        cmp.w   4(a0),d1
        bne.s   1f
        rts
1:      lea     RT_COLSLOT,a1           /* map column -> column sprite (0xFF: none) */
        cmpi.w  #SPR_TM0,d6
        bne.s   0f
        lea     32(a1),a1
0:      move.w  #0,-(sp)
        move.w  d5,d0
        lsr.w   #4,d0
        andi.w  #31,d0                  /* d0 = first map column on screen */
        move.l  (a6),d3                 /* dirty columns */
        clr.l   (a6)
        /* free the column sprites whose map column left the screen (the 21 on screen are a window of
           the 32-column map that wraps around: a fixed column -> sprite mapping cannot work) */
        moveq   #NCOL-1,d7
10:     moveq   #0,d1
        move.b  (a5,d7.w),d1
        bmi.s   11f
        move.w  d1,d2
        sub.w   d0,d2
        andi.w  #31,d2
        cmpi.w  #NCOL,d2
        bcs.s   11f
        st      (a5,d7.w)
        st      (a1,d1.w)
11:     dbra    d7,10b
        moveq   #0,d7                   /* j */
1:      move.w  d0,d1
        add.w   d7,d1
        andi.w  #31,d1                  /* c */
        moveq   #0,d2
        move.b  (a1,d1.w),d2            /* k */
        bpl.s   4f
        moveq   #NCOL-1,d2              /* a free column sprite for it */
5:      tst.b   (a5,d2.w)
        bmi.s   6f
        dbra    d2,5b
        bra.s   3f
6:      move.b  d2,(a1,d1.w)
        bra.s   2f
4:      btst    d1,d3
        beq.s   3f
        movem.l d0/d3-d7/a1,-(sp)       /* a column already in its sprite: only its written rows */
        bsr     col_part
        movem.l (sp)+,d0/d3-d7/a1
        beq.s   3f
        st      (sp)                    /* (col_part did a full upload) */
        bra.s   3f
2:      move.b  d1,(a5,d2.w)
        movem.l d0/d3-d7/a1,-(sp)
        bsr     col_upload
        movem.l (sp)+,d0/d3-d7/a1
        st      (sp)
3:      addq.w  #1,d7
        cmpi.w  #NCOL,d7
        bne.s   1b
        /* positions: rewritten only if the scroll or the crop moved, or a column was reloaded */
        lea     rt_lcache,a0
        cmpi.w  #SPR_TM0,d6
        bne.s   6f
        addq.l  #8,a0
6:      tst.b   (sp)+
        bne.s   7f
        cmp.w   (a0),d5
        bne.s   7f
        cmp.w   2(a0),d4
        bne.s   7f
        move.w  rt_pcrop,d1
        cmp.w   4(a0),d1
        bne.s   7f
        rts
7:      move.w  d5,(a0)
        move.w  d4,2(a0)
        move.w  rt_pcrop,4(a0)
        /* SCB3: every column sprite y | 32 tiles; SCB4: x of the sprite holding map column c */
        move.w  #VRAM_SCB3,d1
        add.w   d6,d1
        move.w  d1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        move.w  d4,d1
        add.w   rt_pcrop,d1
        addi.w  #496,d1                 /* 496 - (-y - crop) */
        andi.w  #0x1FF,d1
        lsl.w   #7,d1
        ori.w   #32,d1
        moveq   #NCOL-1,d2
4:      move.w  d1,REG_VRAMRW
        dbra    d2,4b
        cmpi.w  #SPR_TM0,d6
        bne.s   41f
        /* tilemap 0: its HI column sprites, the same y, 32 tiles if the column has priority-1 cells */
        VRAM_GAP
        move.w  #VRAM_SCB3+SPR_HI,REG_VRAMADDR
        lea     rt_hion,a0
        moveq   #NCOL-1,d2
42:     moveq   #0,d3
        tst.b   (a0)+
        beq.s   43f
        move.w  d1,d3
43:     move.w  d3,REG_VRAMRW
        dbra    d2,42b
41:     /* x: for k = 0..20, the column on screen j with (first + j) mod 21 == k... computed by
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
        cmpi.w  #SPR_TM0,d6
        bne.s   51f
        addi.w  #SPR_HI-SPR_TM0,d3
        VRAM_GAP
        move.w  d3,REG_VRAMADDR
        move.w  d1,REG_VRAMRW
51:     addi.w  #16,d2
        addq.w  #1,d7
        cmpi.w  #NCOL,d7
        bne.s   5b
        rts


* rowptr: a2 = the row pair mask of map column d1 of the layer whose first column sprite is d6
rowptr:
        lea     RT_ROWD,a2
        cmpi.w  #SPR_TM1,d6
        bne.s   1f
        lea     64(a2),a2
1:      adda.w  d1,a2
        adda.w  d1,a2
        rts

* col_part: map column d1 of map a4, in column sprite d6 + d2: only the row pairs written (RT_ROWD).
* Tilemap 0: a priority-1 cell in a column without its HI sprite shown -> col_upload.  NE: full upload
col_part:
        bsr     rowptr
        move.w  (a2),d5
        clr.w   (a2)
        move.w  d1,d3
        lsl.w   #2,d3
        lea     (a4,d3.w),a3            /* the column's cell in row 0 */
        moveq   #0,d4                   /* HI sprite shown */
        cmpi.w  #SPR_TM0,d6
        bne.s   5f
        lea     rt_hion,a0
        move.b  (a0,d2.w),d4
        bne.s   5f
        move.w  d5,d0                   /* no HI sprite: any priority-1 cell in the rows written? */
        movea.l a3,a0
1:      lsr.w   #1,d0
        bcc.s   2f
        move.w  2(a0),d3
        andi.w  #0xC0,d3
        cmpi.w  #0x40,d3
        beq.s   4f
        move.w  128+2(a0),d3
        andi.w  #0xC0,d3
        cmpi.w  #0x40,d3
        beq.s   4f
2:      lea     256(a0),a0
        tst.w   d0
        bne.s   1b
        bra.s   5f
4:      bsr     col_upload
        moveq   #1,d0
        rts
5:      move.w  #1,REG_VRAMMOD
        move.w  d6,d3
        add.w   d2,d3
        lsl.w   #6,d3                   /* SCB1 of the sprite */
        moveq   #0,d7                   /* row pair */
6:      lsr.w   #1,d5
        bcc     8f
        move.w  d7,d0
        lsl.w   #2,d0
        add.w   d3,d0
        VRAM_GAP
        move.w  d0,REG_VRAMADDR
        move.w  d7,d0
        lsl.w   #8,d0
        lea     (a3,d0.w),a0
        bsr   cellpair
        tst.b   d4
        beq.s   8f
        move.w  d7,d0                   /* the HI sprite's two cells */
        lsl.w   #2,d0
        add.w   d3,d0
        addi.w  #(SPR_HI-SPR_TM0)*64,d0
        VRAM_GAP
        move.w  d0,REG_VRAMADDR
        move.w  d7,d0
        lsl.w   #8,d0
        lea     (a3,d0.w),a0
        moveq   #1,d1
7:      move.w  2(a0),d2
        move.w  d2,d0
        andi.w  #0xC0,d0
        cmpi.w  #0x40,d0
        bne.s   71f
        move.w  (a0),d0
        move.w  d0,d6
        lsr.w   #2,d0
        ori.w   #HI_TILES&0xFFFF,d0
        move.w  d0,REG_VRAMRW
        andi.w  #0x3F,d2
        addi.w  #PAL_ARC,d2
        lsl.w   #8,d2
        andi.w  #3,d6
        or.w    d6,d2
        ori.w   #(HI_TILES>>16)<<4,d2
        move.w  d2,REG_VRAMRW
        bra.s   72f
71:     move.w  #HI_BLANK&0xFFFF,REG_VRAMRW
        nop
        nop
        move.w  #(HI_BLANK>>16)<<4,REG_VRAMRW
72:     lea     128(a0),a0
        dbra    d1,7b                   /* (d6 is the caller's, saved) */
8:      addq.w  #1,d7
        tst.w   d5
        bne     6b
        moveq   #0,d0
        rts

* cellpair: the two cells at a0, a0 + 128 -> two SCB1 words each (the address is set)
cellpair:
        moveq   #1,d1
1:      move.w  (a0),d0
        move.w  d0,d2
        lsr.w   #2,d0
        ori.w   #0x4000,d0
        move.w  d0,REG_VRAMRW
        move.w  2(a0),d0
        andi.w  #0x3F,d0
        addi.w  #PAL_ARC,d0
        lsl.w   #8,d0
        andi.w  #3,d2
        or.w    d2,d0
        move.w  d0,REG_VRAMRW
        lea     128(a0),a0
        dbra    d1,1b
        rts

* col_upload: map column d1 of map a4 -> column sprite d6 + d2 (tilemap 0: also its HI sprite)
col_upload:
        bsr     rowptr
        clr.w   (a2)
        move.w  d2,d4                   /* k */
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
        cmpi.w  #SPR_TM0,d6
        bne     9f
        /* HI: the priority-1 cells (their pens 1-7 are above the sprites, MAME squash_state), blank
           elsewhere; the sprite is shown only if there is one */
        lea     -128*32(a0),a0
        move.w  d4,d2
        addi.w  #SPR_HI,d2
        lsl.w   #6,d2
        VRAM_GAP
        move.w  d2,REG_VRAMADDR
        moveq   #0,d5                   /* priority-1 cells */
        moveq   #31,d7
2:      move.w  2(a0),d2
        move.w  d2,d3
        andi.w  #0xC0,d3
        cmpi.w  #0x40,d3
        bne.s   3f
        move.w  (a0),d0
        move.w  d0,d1
        lsr.w   #2,d0
        ori.w   #HI_TILES&0xFFFF,d0
        move.w  d0,REG_VRAMRW
        andi.w  #0x3F,d2
        addi.w  #PAL_ARC,d2
        lsl.w   #8,d2
        andi.w  #3,d1
        or.w    d1,d2
        ori.w   #(HI_TILES>>16)<<4,d2
        move.w  d2,REG_VRAMRW
        addq.w  #1,d5
        bra.s   4f
3:      move.w  #HI_BLANK&0xFFFF,REG_VRAMRW
        nop
        nop
        move.w  #(HI_BLANK>>16)<<4,REG_VRAMRW
4:      lea     128(a0),a0
        dbra    d7,2b
        lea     rt_hion,a0
        tst.w   d5
        sne     (a0,d4.w)
9:      rts

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
        bsr     ytab_sync
        lea     YTAB,a3
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
        lsl.w   #2,d0
        lea     ENT_SH,a1
        adda.w  d0,a1
        lea     FIX_CLRQ,a5
        move.w  #SPR_MAX,d6
        sub.w   d7,d6                   /* Neo Geo sprite of entry k = SPR_MAX-1-k (< SPR_ARC: none) */
        subq.w  #1,d7
0:      subq.l  #2,a6
        move.l  -(a0),d0                /* w2 w3 */
        cmp.l   -(a1),d0
        bne.s   1f
        move.l  -(a0),d0                /* w0 w1 */
        cmp.l   -(a1),d0
        beq     8f                      /* unchanged */
        bra.s   2f
1:      subq.l  #4,a0
        subq.l  #4,a1
2:      move.w  (a0),d0
        move.w  2(a0),2(a1)
        move.w  (a1),d5                 /* entries in the top HUD band: y byte 0xD1-0xE0 (rows 16-31) */
        cmpi.b  #0xD1,d5
        bcs.s   4f
        cmpi.b  #0xE0,d5
        bhi.s   4f
        subq.w  #1,rt_topcnt
4:      cmpi.b  #0xD1,d0
        bcs.s   5f
        cmpi.b  #0xE0,d0
        bhi.s   5f
        addq.w  #1,rt_topcnt
5:      cmpi.w  #1,(a6)                 /* a sprite last frame with the same tile, size, flips and */
        bne.s   6f                      /* colour: only its position moved (the usual case) */
        eor.w   d0,d5
        andi.w  #0xFF00,d5
        bne.s   6f
        move.w  6(a0),d4
        cmp.w   6(a1),d4
        bne.s   6f
        move.w  4(a0),d2
        move.w  4(a1),d3
        eor.w   d2,d3
        andi.w  #0xFE00,d3
        bne.s   6f
        move.w  d0,(a1)
        move.w  d2,4(a1)
        bra     26f
6:      move.w  d0,(a1)
        move.w  4(a0),4(a1)
        move.w  6(a0),6(a1)
        cmpi.w  #1,(a6)
        bls.s   7f                      /* 0: nothing, 1: a sprite */
        move.w  (a6),(a5)+              /* it had a character there */
7:      clr.w   (a6)
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
        moveq   #0,d1
        move.b  1(a0),d1
        neg.w   d1
        addi.w  #240,d1
        andi.w  #0xFF,d1                /* sy */
        moveq   #7,d0
        and.w   d1,d0
        bne     20f
        bsr     get_crop
        sub.w   d5,d1
        bmi     20f
        cmpi.w  #224,d1
        bcc     20f
        lsr.w   #3,d1                   /* row */
        add.w   d1,d3
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
20:     /* a sprite.  d0 = SCB3 word (y | 1 tile, 0: off screen), d3 = SCB4 word, from the tables */
        cmpi.w  #SPR_ARC,d6
        blt     8f                      /* past the Neo Geo sprites */
        moveq   #0,d1
        move.b  1(a0),d1
        add.w   d1,d1
        move.w  (a3,d1.w),d0
        beq     40b
        move.w  4(a0),d2                /* colour | x */
        move.w  d2,d3
        andi.w  #0x1FF,d3
        add.w   d3,d3
        lea     spr_xt,a4
        move.w  (a4,d3.w),d3
        moveq   #0,d4
        move.w  6(a0),d4                /* code */
        move.w  (a0),d5
        rol.w   #2,d5
        andi.w  #3,d5                   /* flips: w0 bit 14 = x, 15 = y */
        btst    #3,(a0)                 /* w0 bit 11: 8x8 */
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
        btst    #0,d5                   /* flipped: the tile is in the top left of its 16x16 one */
        beq.s   24f
        subi.w  #8<<7,d3
24:     btst    #1,d5
        beq.s   25f
        addi.w  #8<<7,d0
25:     /* SCB1 (the last VRAM write is at least a loop turn back) */
        move.w  d6,d1
        lsl.w   #6,d1
        move.w  d1,REG_VRAMADDR
        move.w  d4,REG_VRAMRW
        swap    d4
        lsl.w   #4,d4                   /* tile bits 16-19 */
        or.w    d4,d5
        andi.w  #0x7E00,d2
        lsr.w   #1,d2
        addi.w  #PAL_ARC<<8,d2          /* palette = colour + PAL_ARC */
        or.w    d2,d5
        move.w  d5,REG_VRAMRW
27:     /* SCB3 = d0, SCB4 = d3 (16+ cycles between a data write and the next address write) */
        move.w  #1,(a6)                 /* shown as a sprite */
        move.w  #VRAM_SCB3,d1
        add.w   d6,d1
        move.w  d1,REG_VRAMADDR
        move.w  d0,REG_VRAMRW
        addi.w  #VRAM_SCB4-VRAM_SCB3,d1
        nop
        move.w  d1,REG_VRAMADDR
        move.w  d3,REG_VRAMRW
        bra.s   8f
26:     /* position only: d0 = w0, d2 = w2 */
        moveq   #0,d1
        move.b  d0,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d1
        beq.s   28f
        move.w  d2,d3
        andi.w  #0x1FF,d3
        add.w   d3,d3
        lea     spr_xt,a4
        move.w  (a4,d3.w),d3
        btst    #11,d0
        beq.s   29f
        btst    #14,d0                  /* 8x8: the tile is in the top left of its 16x16 one */
        beq.s   30f
        subi.w  #8<<7,d3
30:     btst    #15,d0
        beq.s   29f
        addi.w  #8<<7,d1
29:     move.w  d1,d0
        bra     27b
28:     clr.w   (a6)
        bra     40b
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
        bne.s   33f
        bsr     fix_clear_stamps
33:     /* layout: game when something is in the top HUD band.  On a change every entry is redone */
        moveq   #0,d0
        tst.w   rt_topcnt
        beq.s   34f
        moveq   #1,d0
34:     cmp.b   rt_mode,d0
        beq.s   32f
        move.b  d0,rt_mode
        move.w  #CROP_Y,rt_pcrop
        tst.b   d0
        beq.s   35f
        move.w  #24,rt_pcrop
35:     lea     ENT_SH,a0
        move.w  #510,d0
36:     move.w  #0xFFFF,(a0)
        addq.l  #8,a0
        dbra    d0,36b
        clr.w   rt_topcnt
        move.w  #511,rt_sprprev
        st      rt_sprdirty
32:     rts

* ytab_sync: YTAB[arcade y byte] = SCB3 word (Neo Geo line from get_crop, 1 tile) or 0 if off screen;
* rebuilt when the layout (rt_mode, rt_pcrop) changed
ytab_sync:
        move.b  rt_mode,d0
        lsl.w   #8,d0
        or.b    rt_pcrop+1,d0
        cmp.w   rt_ytabkey,d0
        beq.s   9f
        move.w  d0,rt_ytabkey
        lea     YTAB,a4
        moveq   #0,d4                   /* y byte */
1:      move.w  #240,d1
        sub.w   d4,d1
        andi.w  #0xFF,d1                /* sy */
        bsr     get_crop
        sub.w   d5,d1                   /* Neo Geo line */
        moveq   #0,d0
        cmpi.w  #224,d1
        bge.s   2f
        cmpi.w  #-16,d1
        ble.s   2f
        move.w  #496,d0
        sub.w   d1,d0
        andi.w  #0x1FF,d0
        lsl.w   #7,d0
        addq.w  #1,d0
2:      move.w  d0,(a4)+
        addq.w  #1,d4
        cmpi.w  #256,d4
        bne.s   1b
9:      rts

* spr_xt: arcade x (9 bits) -> SCB4 word (screen x = x - 15)
spr_xt:
        .set    i,0
        .rept   512
        .word   ((i-15)&0x1FF)<<7
        .set    i,i+1
        .endr

* get_crop: d1.w = sy -> d5.w = arcade row on Neo Geo line 0 for it (see ram.i)
get_crop:
        move.w  rt_pcrop,d5
        tst.b   rt_mode
        beq.s   9f
        cmpi.w  #32,d1
        bcc.s   1f
        moveq   #16,d5                  /* top HUD */
        rts
1:      cmpi.w  #232,d1
        bcs.s   9f
        moveq   #32,d5                  /* bottom HUD */
9:      rts

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


        .if     RT_BUF_END > BIOS_RAM
        .error  "runtime buffers over the BIOS RAM"
        .endif
