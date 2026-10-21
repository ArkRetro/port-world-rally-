* video.s - Neo Geo video layer of the port.
*
* The arcade has two 64x32 tilemaps of 16x16 tiles (1024x512 px, wrap-around scrolling) plus 16x16 sprites.
* Neo Geo has no tilemaps, so each layer is drawn with NCOLSPR column sprites, each 32 tiles (512 px) tall =
* a whole map column; vertical scrolling is just the sprite Y (the 9-bit sprite space wraps at 512 lines),
* horizontal scrolling moves the columns and re-loads one column per 16 px scrolled. The tiles were
* converted 1:1 (gfx2crom.py), so an arcade tile code is a Neo Geo tile number.
*
* Sprite order (= priority): layer 1 (opaque, back) < layer 0 < arcade sprite list < layer-0 "HI" columns.
* The arcade draws the layer-0 tiles whose attribute bit 5 is set (category 1, pens 8-15) above the sprites:
* a third set of column sprites (SPR_HI) repeats layer 0 with only those cells (HI_BLANK elsewhere); a column
* without category-1 cells gets height 0 so it does not count against the 96 sprites per scanline.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global vc_room, video_init, video_init_min, video_vblank, video_frame
        .global pal_flush, pal_write, vram_mark_dirty, video_mark_all_dirty, pal_table, vram_store, vram_flush

* ------------------------------------------------------------------------------------------
* Minimal video init (used by the error screen too): LSPC mode, all sprites off, fix cleared.
* ------------------------------------------------------------------------------------------
video_init_min:
        move.w  #0x4000,REG_LSPCMODE    /* timer off, auto-animation speed 0x40 (same value the BIOS uses) */
        move.b  d0,REG_SHADOW
        move.b  d0,REG_NOSHADOW
        move.b  d0,REG_CRTFIX           /* our S ROM and Z80 */
        move.b  d0,REG_PALBANK0
        /* SCB2 = no shrink, SCB3 = height 0 for sprites 0..511 */
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
        /* fix layer: tile 0xFF (blank), palette 0 */
        move.w  #VRAM_FIX,REG_VRAMADDR
        move.w  #40*32-1,d0
3:      move.w  #0x00FF,REG_VRAMRW
        dbra    d0,3b
        /* fix palette (PAL_FIX): 0 transparent, 1 white, 2 yellow, 3 black, rest grey */
        lea     PALETTES+PAL_FIX*32,a0
        move.w  #0x8000,(a0)+
        move.w  #0x7FFF,(a0)+
        move.w  #0x7FF0,(a0)+
        move.w  #0x8000,(a0)+
        moveq   #11,d0
4:      move.w  #0x4210,(a0)+
        dbra    d0,4b
        /* backdrop (palette 255 colour 15) black; palette 0 colour 0 black */
        move.w  #0x8000,PALETTES+255*32+30
        move.w  #0x8000,PALETTES
        rts

* ------------------------------------------------------------------------------------------
* Full init for the game: minimal init + column sprites + shadow palette + state
* ------------------------------------------------------------------------------------------
video_init:
        bsr     video_init_min
        /* shadow palette: every pen black (pen 9 is dithered in the shadow tile set, pens 10-15 are black) */
        lea     PALETTES+PAL_SHADOW*32,a0
        moveq   #15,d0
1:      move.w  #0x8000,(a0)+
        dbra    d0,1b
        lea     light_pal,a1            /* light palette (night stages) */
        moveq   #15,d0
2:      move.w  (a1)+,(a0)+
        dbra    d0,2b
        /* sprite 0: the LSPC draws it at the end of every scanline list with fewer than 96 sprites (real
           hardware; MAME does not): transparent tiles, off screen */
        move.w  #0,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        moveq   #31,d0
6:      move.w  #HI_BLANK,REG_VRAMRW
        VRAM_GAP
        move.w  #0,REG_VRAMRW
        VRAM_GAP
        dbra    d0,6b
        move.w  #VRAM_SCB4,REG_VRAMADDR
        VRAM_GAP
        move.w  #320<<7,REG_VRAMRW
        VRAM_GAP
        /* every sprite 14 pixels wide (horizontal shrink): the whole 368-pixel arcade width fits in 320 */
        move.w  #VRAM_SCB2+1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        move.w  #380-1,d0
5:      move.w  #SCB2_SHRINK,REG_VRAMRW
        VRAM_GAP
        dbra    d0,5b
        /* column sprites of both layers: height 32 tiles, x off-screen for now */
        move.w  #VRAM_SCB3+SPR_LAYER1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        moveq   #2*NCOLSPR-1,d0
2:      move.w  #0x0020,REG_VRAMRW      /* y=0 (496), not sticky, 32 tiles */
        dbra    d0,2b
        /* no column loaded yet, everything dirty */
        st      rt_col_inval
        lea     rt_col_loaded,a0
        moveq   #2*24/4-1,d0
3:      move.l  #-1,(a0)+
        dbra    d0,3b
        bsr     video_mark_all_dirty
        clr.w   rt_sprites_used
        clr.w   rt_sprites_used1
        clr.w   rt_spr_n0
        clr.w   rt_spr_n1
        move.l  #VCMD_BUF,rt_vcmd_ptr
        move.l  #VCMD_BUF,rt_vcmd_rd
        lea     CELL_ROWS,a0
        move.w  #(CELL_END-CELL_ROWS)/4-1,d0
4:      clr.l   (a0)+
        dbra    d0,4b
        clr.b   rt_pend_n
        clr.b   rt_noseal
        clr.b   rt_fill_set
        move.l  #SPR_F0,rt_set_base
        move.l  #EFF_DESC,rt_eff_base
        clr.l   rt_set_cnt
        clr.l   rt_set_cnt+4
        clr.l   rt_set_cnt+8
        clr.l   rt_set_cnt+12
        move.w  #0xFFFF,rt_lu_force     /* queue the layer positions on the first frame (SCB3 via the */
        moveq   #-1,d0                  /* force, SCB4 through a signature no frame can match) */
        move.l  d0,rt_lu_sig
        move.l  d0,rt_lu_sig+4
        rts

video_mark_all_dirty:
        lea     rt_col_dirty,a0
        moveq   #2*64/4-1,d0
1:      move.l  #0x01010101,(a0)+
        dbra    d0,1b
        lea     rt_pal_dirty,a0
        moveq   #64/4-1,d0
2:      move.l  #0x01010101,(a0)+
        dbra    d0,2b
        st      rt_pal_any
        rts

* ------------------------------------------------------------------------------------------
* pal_write: arcade palette write.  d0.w = entry index (0..8191), d1.w = xBRG444 value.
* Only bank 0 (entries 0..1023) is displayed; the other banks are the arcade's shadow levels.
* Trashes d0-d2/a0.
* ------------------------------------------------------------------------------------------
pal_write:
        and.l   #0x1FFF,d0
        cmpi.w  #0x400,d0
        bcc.s   9f
        and.w   #0x0FFF,d1
        add.w   d1,d1
        lea     pal_table,a0
        move.w  (a0,d1.w),d2
        add.w   d0,d0
        lea     ARC_PAL,a0
        move.w  d2,(a0,d0.w)
        lsr.w   #5,d0                   /* entry*2 / 32 = palette number */
        lea     rt_pal_dirty,a0
        st      (a0,d0.w)
        st      rt_pal_any
9:      rts

* ------------------------------------------------------------------------------------------
* vram_mark_dirty: d0.l = arcade VRAM address (0x100000-0x103FFF) that was written.
* Marks the (layer, column) of that tile dirty.  Trashes d0/a0.
* ------------------------------------------------------------------------------------------
vram_mark_dirty:
        lea     rt_col_dirty,a0
        btst    #13,d0                  /* layer 1 at +0x2000 */
        beq.s   1f
        lea     64(a0),a0
1:      lsr.w   #2,d0                   /* tile index */
        and.w   #63,d0                  /* column */
        st      (a0,d0.w)
        rts

* ------------------------------------------------------------------------------------------
* video_vblank: called from the VBlank handler - flush dirty palettes to palette RAM.
* ------------------------------------------------------------------------------------------
video_vblank:
        /* the 14-pixel shrink of 24 sprites (all of them every 16 frames): the MVS BIOS sets every SCB2
           back to 0x0FFF when a coin goes in, and 16-pixel columns 14 pixels apart cover the edges of
           their neighbours */
        moveq   #0,d0
        move.b  rt_scb2_rot,d0
        addq.b  #1,rt_scb2_rot
        and.w   #15,d0
        move.w  d0,d1
        lsl.w   #4,d0
        lsl.w   #3,d1
        add.w   d1,d0                   /* 24 * group */
        add.w   #VRAM_SCB2+1,d0
        move.w  #1,REG_VRAMMOD
        move.w  d0,REG_VRAMADDR
        moveq   #23,d0
1:      move.w  #SCB2_SHRINK,REG_VRAMRW
        nop
        dbra    d0,1b
        move.w  rt_backdrop,PALETTES+255*32+30
        tst.b   rt_diag_on              /* diagnostic overlay: white text */
        beq.s   1f
        move.l  #0x00007FFF,PALETTES+PAL_FIX*32
        move.l  #0x7FFF7FFF,PALETTES+PAL_FIX*32+4
        move.l  #0x7FFF7FFF,PALETTES+PAL_FIX*32+8
        move.l  #0x00000000,PALETTES+PAL_FIX*32+12
1:      cmpi.w  #0x0123,PALETTES+254*32+2       /* a mark in an unused palette: gone when the MVS BIOS */
        beq.s   2f                              /* took the screen (a coin during the demo) */
        bsr     bios_wiped
2:      tst.b   rt_bios_hold                    /* the screen stays black until the layers are back */
        beq.s   3f
        move.w  #0x8000,PALETTES+255*32+30
        subq.b  #1,rt_bios_hold
        beq.s   5f
        rts                                     /* nothing else this VBlank */
5:
        lea     rt_pal_dirty,a0                 /* then every palette at once */
        moveq   #64/4-1,d0
4:      move.l  #0x01010101,(a0)+
        dbra    d0,4b
        st      rt_pal_any
3:      bsr     pal_flush
        bra     9f
* bios_wiped: the MVS BIOS cleared the palette RAM, every sprite height and shrink and filled the fix layer
* when a coin went in: all of it again at once, the palettes after rt_bios_hold black VBlanks (the 4 palettes
* and 24 shrinks per frame of the rotations faded the screen back in slowly, in odd colours, and the layers
* stayed hidden until the next picture)
bios_wiped:
        move.w  #0x0123,PALETTES+254*32+2
        move.b  #3,rt_bios_hold         /* black for 3 VBlanks: the layers' heights come back with the next */
        move.w  #1,REG_VRAMMOD          /* frame (the BIOS's olive backdrop and fix dots showed meanwhile) */
        move.w  #VRAM_SCB2+1,REG_VRAMADDR
        move.w  #16*24-1,d0
2:      move.w  #SCB2_SHRINK,REG_VRAMRW
        dbra    d0,2b
        VRAM_GAP
        move.w  #VRAM_FIX,REG_VRAMADDR
        move.w  #40*32-1,d0
3:      move.w  #0x00FF,REG_VRAMRW
        dbra    d0,3b
        move.w  #0xFFFF,rt_lu_force     /* the layers' positions and heights again (SCB3 / SCB4) */
        moveq   #-1,d0
        move.l  d0,rt_lu_sig
        move.l  d0,rt_lu_sig+4
        rts

* pal_flush: the dirty palettes (ARC_PAL) to palette RAM
pal_flush:
        tst.b   rt_pal_any
        beq.s   8f
        clr.b   rt_pal_any
        lea     rt_pal_dirty,a0
        lea     ARC_PAL,a1
        lea     PALETTES+PAL_ARC_BASE*32,a2
        lea     PALETTES+PAL_BG*32+30,a4
        moveq   #63,d0
1:      tst.b   (a0)+
        beq.s   2f
        clr.b   -1(a0)
        movem.l (a1),d1-d7/a3           /* 16 words = 8 longs */
        movem.l d1-d7/a3,(a2)
        cmpi.w  #32,d0                  /* tile palette (index 0..31 = 63 - d0)? */
        bcs.s   2f
        move.w  (a1),(a4)               /* backdrop palette: colour 15 = colour 0 */
        /* the backdrop columns depend on whether colour 0 is black: recount only when that changes */
        moveq   #63,d1
        sub.w   d0,d1
        lea     rt_pal0_black,a5
        moveq   #0,d2
        cmpi.w  #0x8000,(a1)
        bne.s   3f
        moveq   #1,d2
3:      cmp.b   (a5,d1.w),d2
        beq.s   2f
        move.b  d2,(a5,d1.w)
        st      rt_bg_recount
2:      lea     32(a1),a1
        lea     32(a2),a2
        lea     32(a4),a4
        dbra    d0,1b
8:      rts
9:      /* rewrite 4 palettes per VBlank (all of them every 16 frames) and the shadow / light palettes:
           the BIOS clears the palette RAM when a coin goes in on MVS, and the converted palettes are
           otherwise only written when the game changes them */
        moveq   #0,d0
        move.b  rt_pal_rot,d0
        addq.b  #1,rt_pal_rot
        and.w   #15,d0
        move.w  d0,d1
        lsl.w   #7,d1                   /* 4 palettes x 32 bytes */
        lea     ARC_PAL,a1
        adda.w  d1,a1
        lea     PALETTES+PAL_ARC_BASE*32,a2
        adda.w  d1,a2
        moveq   #3,d2
4:      movem.l (a1)+,d3-d7/a3-a5
        movem.l d3-d7/a3-a5,(a2)
        lea     32(a2),a2
        dbra    d2,4b
        cmpi.w  #8,d0                   /* tile palettes 0..31: their backdrop colour too */
        bcc.s   6f
        lea     ARC_PAL,a1
        adda.w  d1,a1
        lea     PALETTES+PAL_BG*32+30,a2
        adda.w  d1,a2
        moveq   #3,d2
5:      move.w  (a1),(a2)
        lea     32(a1),a1
        lea     32(a2),a2
        dbra    d2,5b
6:      tst.w   d0
        bne.s   8f
        lea     PALETTES+PAL_SHADOW*32,a0
        moveq   #15,d2
7:      move.w  #0x8000,(a0)+
        dbra    d2,7b
        lea     light_pal,a1
        moveq   #15,d2
7:      move.w  (a1)+,(a0)+
        dbra    d2,7b
8:      rts

* ------------------------------------------------------------------------------------------
* VRAM command buffer.  Everything the frame changes in VRAM is queued (vc_* helpers, a6 = write pointer
* while video_frame runs) and replayed by vram_flush at the start of the next VBlank, so the screen is
* never modified while the LSPC draws it, and no VRAM address/data sequence can be split by the IRQ.
* ------------------------------------------------------------------------------------------

* vc_room: make sure a block of up to 64 data words fits after a6 in the ring: past the end, a wrap mark
* (0xFFFF) and on at VCMD_BUF; when the ring is full (the unplayed commands of up to three frames: rare,
* whole-layer reloads), everything queued is written to VRAM now.  Preserves all registers except a6.
vc_room:
        cmpa.l  #VCMD_END-VCMD_ROOM-2,a6
        bcs.s   1f
        /* wrap - unless the next command to play is in the first block of the ring: then a6 would come
           round onto it and a full ring would look empty, so everything goes out first */
        move.l  d0,-(sp)
        move.l  rt_vcmd_rd,d0
        sub.l   #VCMD_BUF,d0
        cmpi.l  #VCMD_ROOM+2,d0
        bhi.s   2f
        bsr.s   vc_play_all
2:      move.l  (sp)+,d0
        move.w  #0xFFFF,(a6)            /* wrap */
        lea     VCMD_BUF,a6
1:      move.l  d0,-(sp)
        move.l  rt_vcmd_rd,d0
        sub.l   a6,d0
        bls.s   9f                      /* the next command to play is not ahead of a6 (or the ring is empty) */
        cmpi.l  #VCMD_ROOM+2,d0
        bhi.s   9f
        bsr.s   vc_play_all
9:      move.l  (sp)+,d0
        rts
* vc_play_all: write every queued command (up to a6) to VRAM now; the sealed frames keep their sprites.
* Preserves all registers.
vc_play_all:
        movem.l d0-d1/a0-a2,-(sp)
        move    sr,-(sp)
        ori.w   #0x0700,sr              /* a VBlank flush in between would move the VRAM address under us */
        movea.l rt_vcmd_rd,a0
        movea.l a6,a2
        bsr     vc_play
        move.l  a6,rt_vcmd_rd
        move.l  a6,rt_pend_q            /* their commands are written */
        move.l  a6,rt_pend_q+4
        move    (sp)+,sr
        movem.l (sp)+,d0-d1/a0-a2
        rts

* VW_COPY src, cnt, tmp: copy cnt+1 words from (src)+ to the VRAM data port (a1), 16 cycles per word
* ("move.w (src)+,(a1) ; nop": more than the 12 cycles the LSPC needs between two writes; the dbra loop
* took 22), unrolled by 8 with a jump into the block for the remainder.  Trashes cnt and tmp.
        .macro  VW_COPY src, cnt, tmp
        addq.w  #1,\cnt
        moveq   #7,\tmp
        and.w   \cnt,\tmp
        lsr.w   #3,\cnt                 /* whole groups of 8 */
        neg.w   \tmp
        add.w   \tmp,\tmp
        add.w   \tmp,\tmp              /* -4 * remainder */
        jmp     8f(pc,\tmp\().w)
7:      .rept   8
        move.w  (\src)+,(a1)
        nop
        .endr
8:      subq.w  #1,\cnt
        bpl.s   7b
        .endm

* vc_play: write the blocks of the ring from a0 up to a2 to VRAM.  Trashes d0-d1/a0-a1.
vc_play:
        lea     REG_VRAMRW,a1
        bra.s   2f
1:      move.w  (a0)+,d0                /* address */
        cmpi.w  #0xFFFF,d0
        beq.s   3f
        move.w  (a0)+,REG_VRAMMOD
        move.w  (a0)+,d1                /* count - 1 */
        move.w  d0,REG_VRAMADDR         /* (>= 16 cycles after the previous data write) */
        VW_COPY a0, d1, d0
2:      cmpa.l  a2,a0
        bne.s   1b
        rts
3:      lea     VCMD_BUF,a0             /* wrap mark */
        bra.s   2b

* ------------------------------------------------------------------------------------------
* vram_flush: called at the very start of the VBlank handler.  Replays the command queue, then writes
* the two sprite pools prepared by spr_lists / sprites_slow and hides the sprites no longer used.
* Trashes d0-d7/a0-a5.
* ------------------------------------------------------------------------------------------
vram_flush:
        tst.b   rt_pend_n
        bne.s   1f
        /* no frame sealed since the last VBlank: the screen stays as it is - unless no frame work runs at
           all (start-up, BIOS screens): then whatever is queued goes out */
        tst.b   rt_in_hook
        bne.s   0f
        cmpi.b  #2,rt_noseal
        bcc.s   2f
        addq.b  #1,rt_noseal
0:      rts
2:      movea.l rt_vcmd_rd,a0
        movea.l rt_vcmd_ptr,a2
        move.l  a2,rt_vcmd_rd
        bra     vc_play
1:      /* the oldest sealed frame: its commands, then its sprite set */
        addq.l  #1,rt_shown
        movea.l rt_vcmd_rd,a0
        movea.l rt_pend_q,a2
        move.l  a2,rt_vcmd_rd
        bsr     vc_play
        moveq   #0,d0
        move.b  rt_pend_s,d0
        move.l  rt_pend_q+4,rt_pend_q
        move.b  rt_pend_s+1,rt_pend_s
        subq.b  #1,rt_pend_n
        cmpi.b  #0xFF,d0
        beq     9f                      /* no sprites converted for that frame: they stay as they are */
        lea     rt_set_cnt,a3
        lsl.w   #3,d0
        adda.w  d0,a3                   /* the set's counts */
        lea     EFF_DESC,a4
        movea.l #SPR_F0,a5
        tst.w   d0
        beq.s   3f
        lea     EFF_SET1,a4
        movea.l #SPR_SET1,a5
3:      move.w  4(a3),d7                /* headlight columns */
        move.w  2(a3),-(sp)             /* pool 1 sprites */
        lea     REG_VRAMRW,a1
        move.w  #SPR_OBJ,d0
        move.w  (a3),d1
        movea.l a5,a0
        move.w  #SPR_F0_SZ,d2
        lea     rt_sprites_used,a2
        bsr     vf_pool
        move.w  #SPR_PRI1,d0
        move.w  (sp)+,d1
        lea     SPR_F1_OFS(a5),a0
        move.w  #SPR_F1_SZ,d2
        lea     rt_sprites_used1,a2
        bsr     vf_pool
* vf_eff: the headlight columns of the set (a4 = its EFF_DESC, d7 = columns): their tiles, SCB3 and
* SCB4, and the columns used last time but not now hidden.
vf_eff:
        move.w  d7,d4
        move.w  rt_eff_used,d5
        move.w  d4,rt_eff_used
        subq.w  #1,d4
        bmi     5f
        move.w  #1,REG_VRAMMOD
        movea.l a4,a0
        lea     EFF_TILES_OFS(a4),a3
        move.w  #SPR_EFF<<6,d3          /* SCB1 of the first column */
        move.w  d4,d1
1:      move.w  d3,REG_VRAMADDR
        move.w  4(a0),d0
        lsl.w   #2,d0
        lea     (a3,d0.w),a2
        move.w  6(a0),d0
        add.w   d0,d0
        subq.w  #1,d0                   /* words - 1 */
        VW_COPY a2, d0, d6
        add.w   #64,d3
        addq.l  #8,a0
        dbra    d1,1b
        move.w  #VRAM_SCB3+SPR_EFF,REG_VRAMADDR
        movea.l a4,a0
        move.w  d4,d1
2:      move.w  (a0),(a1)
        addq.l  #8,a0
        dbra    d1,2b
        move.w  #VRAM_SCB4+SPR_EFF,REG_VRAMADDR
        lea     2(a4),a0
        move.w  d4,d1
3:      move.w  (a0),(a1)
        addq.l  #8,a0
        dbra    d1,3b
5:      addq.w  #1,d4                   /* columns now */
        sub.w   d4,d5                   /* columns to hide */
        ble.s   9f
        move.w  #1,REG_VRAMMOD
        add.w   #VRAM_SCB3+SPR_EFF,d4
        VRAM_GAP
        move.w  d4,REG_VRAMADDR
        subq.w  #1,d5
6:      move.w  #0,(a1)
        dbra    d5,6b
9:      rts
* vf_pool: d0 = first sprite, d1 = count, a0 = fields (4 arrays of d2 bytes), a1 = REG_VRAMRW,
* a2 -> end of the pool as written last time (updated).
vf_pool:
        move.w  d1,d5
        add.w   d0,d5                   /* new end */
        subq.w  #1,d1
        bmi     5f
        move.w  d0,d3
        lsl.w   #6,d3                   /* SCB1 of the first sprite */
        move.w  #64,REG_VRAMMOD
        move.w  d3,REG_VRAMADDR
        movea.l a0,a3
        move.w  d1,d4
        VW_COPY a3, d4, d6               /* tiles (SCB1 even words) */
        addq.w  #1,d3
        lea     (a0,d2.w),a3
        move.w  d3,REG_VRAMADDR
        move.w  d1,d4
        VW_COPY a3, d4, d6               /* attributes (SCB1 odd words) */
        move.w  #1,REG_VRAMMOD
        move.w  d0,d3
        add.w   #VRAM_SCB3,d3
        lea     (a0,d2.w),a3
        adda.w  d2,a3
        move.w  d3,REG_VRAMADDR
        move.w  d1,d4
        VW_COPY a3, d4, d6               /* SCB3 */
        add.w   #VRAM_SCB4-VRAM_SCB3,d3
        lea     (a0,d2.w),a3
        adda.w  d2,a3
        adda.w  d2,a3
        move.w  d3,REG_VRAMADDR
        move.w  d1,d4
        VW_COPY a3, d4, d6               /* SCB4 */
5:      /* hide the sprites of the pool used last time but not now */
        move.w  (a2),d4
        move.w  d5,(a2)
        sub.w   d5,d4
        ble.s   9f
        subq.w  #1,d4
        move.w  #1,REG_VRAMMOD
        add.w   #VRAM_SCB3,d5
        VRAM_GAP
        move.w  d5,REG_VRAMADDR
6:      move.w  #0,(a1)
        dbra    d4,6b
9:      rts

* ------------------------------------------------------------------------------------------
* video_frame: called once per frame (VBlank hook) after the arcade IRQ handler ran: prepares the next
* frame's VRAM contents (queued for vram_flush).
* ------------------------------------------------------------------------------------------
video_frame:
        movea.l rt_vcmd_ptr,a6
        cmpa.l  #VCMD_BUF,a6
        bcc.s   1f
        lea     VCMD_BUF,a6
1:      tst.b   rt_bg_recount
        beq.s   2f
        clr.b   rt_bg_recount
        bsr     bg_recount
2:      /* layer 1 (back) first */
        move.w  ARC_VREGS+6,d0          /* scrollx layer 1 */
        move.w  ARC_VREGS+4,d1          /* scrolly layer 1 */
        lea     ARC_VRAM+0x2000,a3
        lea     rt_c0+2,a4
        lea     rt_col_loaded+24,a5
        move.w  #SPR_LAYER1,d7
        bsr     layer_update
        move.w  REG_LSPCMODE,rt_dbg+8   /* profiling */
        /* backdrop colour = colour 0 of the layer-1 cell at the screen centre: it shows through the
           pen-0 pixels of layer 1 that have no backdrop column (sparse ones, screen flashes) */
        move.w  rt_c0+2,d0
        add.w   #NCOLSPR/2,d0
        and.w   #63,d0
        move.w  ARC_VREGS+4,d1
        add.w   #CROP_Y+112,d1
        lsr.w   #4,d1
        and.w   #31,d1
        lsl.w   #6,d1
        add.w   d1,d0
        lsl.w   #2,d0
        lea     ARC_VRAM+0x2000,a0
        move.b  3(a0,d0.w),d1
        and.w   #0x1F,d1
        lsl.w   #5,d1
        lea     ARC_PAL,a0
        move.w  (a0,d1.w),rt_backdrop
        move.w  ARC_VREGS+2,d0          /* scrollx layer 0 (+4 as in MAME) */
        addq.w  #4,d0
        move.w  ARC_VREGS+0,d1
        lea     ARC_VRAM,a3
        lea     rt_c0,a4
        lea     rt_col_loaded,a5
        move.w  #SPR_LAYER0,d7
        bsr     layer_update
        bsr     cells_flush
        move.l  a6,rt_vcmd_ptr
        move.b  rt_cl_more,rt_col_inval /* columns left over: pass 1 checks them again next frame */
        clr.b   rt_cl_more
        move.b  #CL_BUDGET,rt_cl_budget
        move.w  REG_LSPCMODE,rt_dbg+10  /* profiling */
        /* the sprites were converted by spr_lists inside the arcade interrupt, unless the lists had more
           high-priority sprites than pool 1 holds */
        tst.b   rt_spr_slow
        beq.s   1f
        clr.b   rt_spr_slow
        bra     sprites_slow
1:      rts

* layer_update: d0 = scrollx, d1 = scrolly, a3 = map base (shadow VRAM), a4 = rt_c0 slot of the layer,
*               a5 = loaded-column table, d7 = first Neo Geo sprite of the layer, a6 = command queue.
* Pass 1 (re)loads the column sprites whose map column changed (col_load); pass 2 queues the positions of
* the layer's NCOLSPR sprites and of its companion set (HI for layer 0, BG for layer 1), only when they
* changed (scroll, a reloaded column, a companion column shown or hidden).
layer_update:
        add.w   #CROP_X,d0
        and.w   #0x3FF,d0
        add.w   #CROP_Y,d1
        cmpi.w  #SPR_LAYER0,d7
        bne.s   8f
        move.w  d1,d2                   /* the HI columns only hold the rows from screen line HI_TOP down: */
        add.w   #HI_TOP,d2              /* the HUD lines carry 48 layer sprites and up to 30 HUD ones, and */
        lsr.w   #4,d2                   /* HI columns there (cells above the sprites: trees, the finish */
        and.w   #31,d2                  /* banner) pushed them past 96 per line - letters lost */
        cmp.b   rt_hi_vt,d2
        beq.s   8f
        move.b  d2,rt_hi_vt
        move.b  #NCOLSPR,rt_hi_pend     /* re-range every HI column, a quarter of them per frame */
8:      tst.b   rt_hi_pend
        beq.s   9f
        bsr     hi_vt_moved
9:        add.w   #496,d1
        and.w   #0x1FF,d1
        lsl.w   #7,d1
        or.w    #0x20,d1                /* SCB3 value: y | height 32 */
        move.w  d1,d6
        move.w  d0,d2
        and.w   #15,d2                  /* fine scroll f */
        move.w  d2,d1
        lsl.w   #3,d2
        sub.w   d1,d2
        addq.w  #4,d2
        lsr.w   #3,d2                   /* round(f * 7/8): the columns are 14 pixels wide */
        neg.w   d2
        movea.w d2,a2                   /* a2 = x of window column 0 */
        move.w  d0,d3
        lsr.w   #4,d3                   /* first map column (0..63) */
        /* c0 counts the columns without the 64-column wrap of the map, modulo 192 (a multiple of 64 and
           of NCOLSPR): column sprite s = c mod NCOLSPR then stays the same when the scroll wraps (with
           c mod 64, 64 not being a multiple of NCOLSPR, every column moved to another sprite and the
           whole layer was reloaded - more than two frames of work) */
        move.w  (a4),d4
        move.w  d3,d5
        sub.w   d4,d5
        and.w   #63,d5                  /* the step since last frame, -32..31 */
        cmpi.w  #32,d5
        bcs.s   1f
        sub.w   #64,d5
1:      add.w   d5,d4
        bpl.s   2f
        add.w   #192,d4
2:      cmpi.w  #192,d4
        bcs.s   3f
        sub.w   #192,d4
3:      move.w  d4,d3                   /* c0 (0..191; = the map column mod 64) */
        move.w  d3,(a4)                 /* rt_c0 of this layer, for vram_store */
        lea     mod_ncol,a0
        moveq   #0,d4
        move.b  (a0,d3.w),d4            /* s0 = sprite of window column 0 */
        lea     tile_attr_table,a1
        /* pass 1: columns whose map column changed - nothing to do when the first map column is the one
           checked last time and no column was invalidated since (only col_load changes the table then) */
        lea     rt_lu_c0last,a0
        cmpi.w  #SPR_LAYER0,d7
        beq.s   1f
        addq.l  #2,a0
1:      tst.b   rt_col_inval
        bne.s   2f
        cmp.w   (a0),d3
        beq.s   lu_p1done
2:      move.w  d3,(a0)
        move.w  d4,d0                   /* s */
        move.w  d3,d1                   /* unwrapped map column */
        moveq   #NCOLSPR-1,d5
lu_chk:
        move.w  d1,d3
        and.w   #63,d3
        cmp.b   (a5,d0.w),d3
        beq.s   1f
        subq.b  #1,rt_cl_budget
        bcs.s   3f
        bsr     col_load                /* d0 = s, d3 = map column */
        bra.s   1f
3:      clr.b   rt_cl_budget            /* over this frame's budget: the next frame loads it */
        st      rt_cl_more
1:      addq.w  #1,d1
        addq.w  #1,d0
        cmpi.w  #NCOLSPR,d0
        bcs.s   2f
        moveq   #0,d0
2:      dbra    d5,lu_chk
lu_p1done:
        /* companion columns with cells that changed category: recount them (rt_cmp_any: any marked) */
        lea     CMP_HI_DIRTY,a0
        lea     rt_cmp_any,a4
        cmpi.w  #SPR_LAYER0,d7
        beq.s   1f
        lea     CMP_BG_DIRTY,a0
        addq.l  #1,a4
1:      tst.b   (a4)
        beq     lu_p2
        clr.b   (a4)
        moveq   #0,d0
2:      move.b  (a0,d0.w),d2
        beq.s   4f
        clr.b   (a0,d0.w)
        moveq   #0,d1
        move.b  (a5,d0.w),d1
        cmpi.b  #0xFF,d1
        beq.s   4f
        cmpi.w  #SPR_LAYER0,d7
        bne.s   3f
        bsr     hi_count
        bra.s   4f
3:      bsr     bg_count
4:      addq.w  #1,d0
        cmpi.w  #NCOLSPR,d0
        bcs.s   2b
lu_p2:  /* pass 2: skip when nothing moved (same x0, same y, no forced update) */
        lea     rt_lu_sig,a0
        lea     rt_lu_force,a4
        cmpi.w  #SPR_LAYER0,d7
        beq.s   3f
        addq.l  #4,a0
        addq.l  #1,a4
3:      /* x signature: s0 | fine x (a whole-column step changes s0, not x0); y signature: SCB3 y.  A moved
           x rewrites the SCB4 blocks, a moved y or a forced update (rows shown / companions changed) the
           SCB3 blocks: horizontal or vertical scrolling alone writes half of the words */
        moveq   #0,d1                   /* bit 0: SCB4, bit 1: SCB3 */
        move.w  d4,d0
        lsl.w   #8,d0
        move.w  a2,d2
        move.b  d2,d0
        cmp.w   (a0),d0
        beq.s   4f
        move.w  d0,(a0)
        moveq   #1,d1
4:      cmp.w   2(a0),d6
        beq.s   5f
        move.w  d6,2(a0)
        bset    #1,d1
5:      tst.b   (a4)
        beq.s   6f
        clr.b   (a4)
        bset    #1,d1
6:      tst.b   d1
        beq     lu_done
        move.w  d1,-(sp)
        btst    #0,d1
        beq     lu_scb3
        /* SCB4 of the layer's sprites: x of sprite s = x0 + 14 * ((s - s0) mod NCOLSPR), in the SCB4 form
           (x << 7: the 9-bit wrap of x comes with the 16-bit word), then the same block for the companion set */
        bsr     vc_room
        move.w  d7,d0
        add.w   #VRAM_SCB4,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #NCOLSPR-1,(a6)+
        movea.l a6,a0                   /* the x words, copied for the companion set below */
        move.w  a2,d3
        lsl.w   #7,d3                   /* x of s0 */
        move.w  #14<<7,d1
        moveq   #NCOLSPR,d0
        sub.w   d4,d0
        mulu    #14<<7,d0
        add.w   d3,d0                   /* x of sprite 0 (window column NCOLSPR - s0) */
        move.w  d4,d5
        subq.w  #1,d5
        bmi.s   2f
1:      move.w  d0,(a6)+                /* sprites 0 .. s0-1 */
        add.w   d1,d0
        dbra    d5,1b
2:      moveq   #NCOLSPR-1,d5
        sub.w   d4,d5
3:      move.w  d3,(a6)+                /* sprites s0 .. NCOLSPR-1 */
        add.w   d1,d3
        dbra    d5,3b
        move.w  #SPR_HI,d3
        cmpi.w  #SPR_LAYER0,d7
        beq.s   4f
        move.w  #SPR_BG,d3
4:      move.w  d3,d0                   /* companion base */
        add.w   #VRAM_SCB4,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #NCOLSPR-1,(a6)+
        moveq   #NCOLSPR/2-1,d5
5:      move.l  (a0)+,(a6)+
        dbra    d5,5b
lu_scb3:
        btst    #1,1(sp)
        beq     lu_p2end
        move.w  #SPR_HI,d3              /* companion base (also set by the SCB4 part) */
        cmpi.w  #SPR_LAYER0,d7
        beq.s   1f
        move.w  #SPR_BG,d3
1:      bsr     vc_room
        /* SCB3: the layer: y | rows to show (32, fewer when the bottom of the column is empty, 0 when it
           is all empty: such columns do not count against the 96 sprites per scanline) */
        move.w  d7,d0
        add.w   #VRAM_SCB3,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #NCOLSPR-1,(a6)+
        lea     LAY_H,a0
        cmpi.w  #SPR_LAYER0,d7
        beq.s   1f
        lea     24(a0),a0
1:      move.w  d6,d1
        and.w   #0xFF80,d1
        moveq   #NCOLSPR-1,d5
4:      moveq   #0,d2
        move.b  (a0)+,d2
        or.w    d1,d2
        move.w  d2,(a6)+
        dbra    d5,4b
        /* the companion set: y of its first row, height = its row range; height 0 when empty */
        bsr     vc_room
        move.w  d3,d0
        add.w   #VRAM_SCB3,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #NCOLSPR-1,(a6)+
        lea     rt_hi_cnt,a0
        lea     CMP_HI_R0,a1
        lea     CMP_HI_H,a4
        cmpi.w  #SPR_LAYER0,d7
        beq.s   1f
        lea     rt_bg_cnt,a0
        lea     CMP_BG_R0,a1
        lea     CMP_BG_H,a4
1:      move.w  d6,d1
        and.w   #0xFF80,d1              /* y of row 0 | height 0 */
        moveq   #NCOLSPR-1,d5
5:      tst.b   (a0)+
        beq.s   6f
        moveq   #0,d2
        move.b  (a1),d2
        lsl.w   #8,d2
        lsl.w   #3,d2                   /* r0 * 16 lines << 7 */
        move.w  d1,d4
        sub.w   d2,d4                   /* (the 9-bit y field wraps by itself) */
        moveq   #0,d2
        move.b  (a4),d2
        or.w    d2,d4
        move.w  d4,(a6)+
        addq.l  #1,a1
        addq.l  #1,a4
        dbra    d5,5b
        bra.s   lu_p2end
6:      move.w  d1,(a6)+
        addq.l  #1,a1
        addq.l  #1,a4
        dbra    d5,5b
lu_p2end:
        addq.l  #2,sp
lu_done:
        lea     tile_attr_table,a1
        rts

* col_load: d0 = s, d3 = map column: queue the column for sprite d7+s and refresh its companion (HI/BG)
* column (count + upload when it has something to show).  Preserves d0-d6/a0/a2 (a1 = tile_attr_table,
* a3 = map base, a5 = loaded table, d7 = layer base as in layer_update; a6 advances).
col_load:
        movem.l d0-d6/a0/a2,-(sp)
        bsr     vc_room
        move.b  d3,(a5,d0.w)            /* loaded[s] = column */
        move.w  d0,d5                   /* s */
        move.w  d3,d1                   /* map column */
        move.w  d1,d2
        lsl.w   #2,d2
        lea     (a3,d2.w),a0            /* a0 = map column (row 0) */
        add.w   d7,d0
        lsl.w   #6,d0                   /* SCB1 address of the sprite */
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #63,(a6)+
        lea     pen0_table,a2
        moveq   #0,d3                   /* rows to show: past the last non-empty one */
        moveq   #31,d2
4:      move.l  (a0),d4
        swap    d4
        and.w   #0x3FFF,d4
        move.w  d4,(a6)+
        btst    #1,(a2,d4.w)
        bne.s   5f                      /* fully transparent tile */
        moveq   #32,d3
        sub.w   d2,d3
5:      moveq   #0,d4
        move.b  3(a0),d4
        add.w   d4,d4
        move.w  (a1,d4.w),(a6)+
        lea     256(a0),a0
        dbra    d2,4b
        lea     LAY_H,a2
        move.w  d5,d0
        cmpi.w  #SPR_LAYER0,d7
        beq.s   6f
        add.w   #24,d0
6:      move.b  d3,(a2,d0.w)            /* (pass 2 is forced by hi_count / bg_count below) */
        move.w  d5,d0
        cmpi.w  #SPR_LAYER0,d7
        bne.s   1f
        bsr     hi_count
        bra.s   2f
1:      bsr     bg_count
2:      movem.l (sp)+,d0-d6/a0/a2
        rts

* hi_count: d0 = s, d1 = map column (layer 0): HI_MASK[s] = the rows with category-1 cells, then hi_range.
* Forces the positions to be queued again.  Preserves registers except a6.
hi_count:
        movem.l d0-d7/a0-a1,-(sp)
        move.w  d1,d2
        lsl.w   #2,d2
        lea     ARC_VRAM+31*256,a1
        adda.w  d2,a1                   /* the column, row 31 */
        moveq   #0,d3                   /* mask */
        moveq   #31,d2
1:      add.l   d3,d3
        btst    #5,3(a1)
        beq.s   2f
        addq.w  #1,d3
2:      lea     -256(a1),a1
        dbra    d2,1b
        move.w  d0,d2
        lsl.w   #2,d2
        lea     HI_MASK,a0
        move.l  d3,(a0,d2.w)
        st      rt_lu_force
        moveq   #1,d7                   /* queue the column even with the same rows (its cells changed) */
        bsr.s   hi_range
        movem.l (sp)+,d0-d7/a0-a1
        rts

* hi_range: d0 = s, d1 = map column, d7 = 1 to queue the column whatever its rows: the HI column holds the
* category-1 rows among the HI_ROWS from rt_hi_vt (the screen below line HI_TOP; a range over all 32 rows covered the
* HUD lines with blank tiles and pushed its sprites past 96 per line).  rt_hi_cnt[s] = its height (0: none),
* CMP_HI_R0/H[s] = its rows (they may wrap past row 31); queued when they changed.  Trashes d2-d6/a0.
hi_range:
        move.w  d0,d2
        lsl.w   #2,d2
        lea     HI_MASK,a0
        move.l  (a0,d2.w),d3
        moveq   #0,d6
        move.b  rt_hi_vt,d6
        ror.l   d6,d3                   /* bit i = row vt + i */
        and.l   #(1<<HI_ROWS)-1,d3
        moveq   #0,d4                   /* first */
        moveq   #0,d5                   /* height */
        tst.l   d3
        beq.s   3f
1:      lsr.l   #1,d3
        bcs.s   2f
        addq.w  #1,d4
        bra.s   1b
2:      moveq   #1,d5                   /* rows first .. last */
        bra.s   5f
4:      addq.w  #1,d2
        lsr.l   #1,d3
        bcc.s   4b
        add.w   d2,d5
5:      moveq   #0,d2
        tst.l   d3
        bne.s   4b
        add.w   d6,d4
        and.w   #31,d4                  /* first row */
3:      lea     rt_hi_cnt,a0
        move.b  (a0,d0.w),d2            /* previous height */
        move.b  d5,(a0,d0.w)
        tst.w   d7
        bne.s   6f
        tst.w   d5
        bne.s   7f
        tst.b   d2
        beq.s   9f                      /* still empty */
        st      rt_lu_force             /* hidden */
        rts
7:      tst.b   d2
        beq.s   6f                      /* shown again */
        lea     CMP_HI_R0,a0
        cmp.b   (a0,d0.w),d4
        bne.s   6f
        lea     CMP_HI_H,a0
        moveq   #0,d2
        move.b  (a0,d0.w),d2
        cmp.w   d2,d5
        beq.s   9f                      /* same rows */
        st      rt_lu_force
        move.b  d5,(a0,d0.w)
        sub.w   d2,d5
        bls.s   9f                      /* fewer rows: the height alone */
        move.w  d2,d4                   /* more rows below: only the new slots */
        bra     hi_upload_part
6:      st      rt_lu_force
        tst.w   d5
        beq.s   9f
        lea     CMP_HI_R0,a0
        move.b  d4,(a0,d0.w)
        lea     CMP_HI_H,a0
        move.b  d5,(a0,d0.w)
        bsr     hi_upload
9:      rts

* hi_vt_moved: the window moved (a5 = loaded-column table): the next 6 HI columns of the rt_hi_pend left are
* re-ranged and queued again when their rows change.  Preserves d0/d1/d7.
hi_vt_moved:
        movem.l d0-d1/d7,-(sp)
        moveq   #0,d7
        moveq   #NCOLSPR,d0
        sub.b   rt_hi_pend,d0
        subq.b  #6,rt_hi_pend
        bcc.s   1f
        clr.b   rt_hi_pend
1:      moveq   #0,d1
        move.b  (a5,d0.w),d1
        cmpi.b  #0xFF,d1
        beq.s   2f
        bsr     hi_range
2:      addq.w  #1,d0
        cmpi.w  #NCOLSPR,d0
        bcc.s   3f
        moveq   #NCOLSPR,d1
        sub.b   rt_hi_pend,d1
        cmp.w   d1,d0
        bcs.s   1b
3:      movem.l (sp)+,d0-d1/d7
        rts

* hi_upload: d0 = s, d1 = map column: queue SCB1 of HI sprite SPR_HI+s = rows r0..r0+h-1 of the column
* (category-1 tiles, HI_BLANK for the others).  hi_upload_part: only slots d4 .. d4+d5-1.
* Preserves registers except a6.
hi_upload:
        movem.l d4-d5,-(sp)
        moveq   #0,d4
        lea     CMP_HI_H,a0
        moveq   #0,d5
        move.b  (a0,d0.w),d5
        bsr.s   hi_upload_part
        movem.l (sp)+,d4-d5
        rts
hi_upload_part:
        movem.l d0-d4/a0-a1,-(sp)
        bsr     vc_room
        lea     CMP_HI_R0,a0
        moveq   #0,d3
        move.b  (a0,d0.w),d3
        add.w   d4,d3
        and.w   #31,d3                  /* first row (the range may wrap past row 31) */
        move.w  d5,d2
        move.w  d1,d1
        lsl.w   #2,d1
        lea     ARC_VRAM,a0
        adda.w  d1,a0
        move.w  d3,d1                   /* row */
        lsl.w   #8,d3
        adda.w  d3,a0
        lea     tile_attr_table,a1
        add.w   #SPR_HI,d0
        lsl.w   #6,d0
        add.w   d4,d0
        add.w   d4,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  d2,d4
        add.w   d4,d4
        subq.w  #1,d4
        move.w  d4,(a6)+
        subq.w  #1,d2
1:      move.l  (a0),d4
        btst    #5,d4
        beq.s   2f
        swap    d4
        and.w   #0x3FFF,d4
        bra.s   3f
2:      move.w  #HI_BLANK,d4
3:      move.w  d4,(a6)+
        moveq   #0,d4
        move.b  3(a0),d4
        add.w   d4,d4
        move.w  (a1,d4.w),(a6)+
        lea     256(a0),a0
        addq.w  #1,d1
        cmpi.w  #32,d1
        bcs.s   4f
        moveq   #0,d1
        lea     -32*256(a0),a0
4:      dbra    d2,1b
        movem.l (sp)+,d0-d4/a0-a1
        rts

* bg_count: d0 = s, d1 = map column (layer 1): rt_bg_cnt[s] = cells needing the backdrop, CMP_BG_R0/H[s] =
* their row range; queues the BG column (those rows) when there are some.  Preserves registers except a6.
bg_count:
        movem.l d0-d6/a0-a3,-(sp)
        movea.w d1,a1                   /* keep the column */
        move.w  d1,d2
        lsl.w   #2,d2
        lea     ARC_VRAM+0x2000,a0
        adda.w  d2,a0
        lea     pen0_table,a2
        lea     ARC_PAL,a3
        move.w  d0,d6                   /* s */
        moveq   #0,d3                   /* count */
        moveq   #-1,d4                  /* first row */
        moveq   #0,d5                   /* last row */
        moveq   #0,d2                   /* row */
1:      move.l  (a0),d0                 /* (bg_sig inline) */
        moveq   #0x1F,d1
        and.w   d0,d1
        lsl.w   #5,d1
        cmpi.w  #0x8000,(a3,d1.w)
        beq.s   2f                      /* colour 0 black */
        swap    d0
        and.w   #0x3FFF,d0
        btst    #0,(a2,d0.w)
        beq.s   2f                      /* no pen-0 pixels */
        addq.w  #1,d3
        tst.w   d4
        bpl.s   3f
        move.w  d2,d4
3:      move.w  d2,d5
2:      lea     256(a0),a0
        addq.w  #1,d2
        cmpi.w  #32,d2
        bcs.s   1b
        move.w  d6,d0
        move.w  a1,d1
        lea     rt_bg_cnt,a0
        move.b  d3,(a0,d0.w)
        st      rt_lu_force+1
        tst.w   d3
        beq.s   4f
        lea     CMP_BG_R0,a0
        move.b  d4,(a0,d0.w)
        sub.w   d4,d5
        addq.w  #1,d5
        lea     CMP_BG_H,a0
        move.b  d5,(a0,d0.w)
        bsr     bg_upload
4:      movem.l (sp)+,d0-d6/a0-a3
        rts

* bg_upload: d0 = s, d1 = map column: queue SCB1 of backdrop sprite SPR_BG+s = rows r0..r0+h-1 (solid
* tile, palette PAL_BG + colour of each cell).  Preserves registers except a6.
bg_upload:
        movem.l d0-d4/a0,-(sp)
        bsr     vc_room
        lea     CMP_BG_R0,a0
        moveq   #0,d3
        move.b  (a0,d0.w),d3
        lea     CMP_BG_H,a0
        moveq   #0,d2
        move.b  (a0,d0.w),d2
        move.w  d1,d4
        lsl.w   #2,d4
        lea     ARC_VRAM+0x2000,a0
        adda.w  d4,a0
        lsl.w   #8,d3
        adda.w  d3,a0
        add.w   #SPR_BG,d0
        lsl.w   #6,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  d2,d4
        add.w   d4,d4
        subq.w  #1,d4
        move.w  d4,(a6)+
        subq.w  #1,d2
1:      move.w  #SOLID_TILE,(a6)+
        moveq   #0,d4
        move.b  3(a0),d4
        and.w   #0x1F,d4
        add.w   #PAL_BG,d4
        lsl.w   #8,d4
        move.w  d4,(a6)+
        lea     256(a0),a0
        dbra    d2,1b
        movem.l (sp)+,d0-d4/a0
        rts

* cells_flush: the map cells marked by vram_store, row by row: each run of consecutive marked column
* sprites is one block of tiles and one of attributes (VRAMMOD 64 = from one sprite to the next).  Read
* from the shadow map now (the current pairs).  a6 = command queue.  Trashes d0-d7/a0-a5.
cells_flush:
        moveq   #0,d7                   /* 0: layer 0, 4: layer 1 */
cf_layer:
        lea     CELL_ROWS,a0
        move.l  (a0,d7.w),d6
        beq     cf_next
        clr.l   (a0,d7.w)
        lea     CELL_MASK,a4
        lea     rt_col_loaded,a5
        lea     ARC_VRAM,a3
        move.w  #SPR_LAYER0,d5
        tst.w   d7
        beq.s   1f
        lea     128(a4),a4
        lea     24(a5),a5
        lea     ARC_VRAM+0x2000,a3
        move.w  #SPR_LAYER1,d5
1:      moveq   #0,d4                   /* row */
cf_row:
        lsr.l   #1,d6
        bcc     cf_rnext
        move.w  d4,d0
        lsl.w   #2,d0
        move.l  (a4,d0.w),d3            /* sprites marked in this row */
        clr.l   (a4,d0.w)
        moveq   #0,d2                   /* s */
cf_bit:
        btst    d2,d3
        beq.s   cf_skip
        cmpi.b  #0xFF,(a5,d2.w)
        beq.s   cf_skip                 /* not loaded: col_load writes it whole */
        move.w  d2,d1                   /* end of the run */
2:      addq.w  #1,d1
        cmpi.w  #NCOLSPR,d1
        bcc.s   3f
        btst    d1,d3
        beq.s   3f
        cmpi.b  #0xFF,(a5,d1.w)
        bne.s   2b
3:      bsr.s   cf_emit                 /* sprites d2 .. d1-1 */
        move.w  d1,d2
        bra.s   cf_test
cf_skip:
        addq.w  #1,d2
cf_test:
        cmpi.w  #NCOLSPR,d2
        bcs.s   cf_bit
cf_rnext:
        addq.w  #1,d4
        tst.l   d6
        bne     cf_row
cf_next:
        addq.w  #4,d7
        cmpi.w  #8,d7
        bcs     cf_layer
        rts
* cf_emit: run of sprites d2 .. d1-1 at row d4 (d5 = first sprite of the layer, a3 = map, a5 = loaded
* table): two blocks, tiles then attributes.  Preserves d1-d7.
cf_emit:
        movem.l d1-d3/d6,-(sp)
        bsr     vc_room
        move.w  d5,d0
        add.w   d2,d0
        lsl.w   #6,d0
        move.w  d4,d3
        add.w   d3,d3
        add.w   d3,d0                   /* SCB1 word of (first sprite, row): tile */
        movea.w d0,a0
        move.w  d1,d6
        sub.w   d2,d6
        subq.w  #1,d6                   /* words - 1 */
        move.w  d4,d3
        lsl.w   #8,d3
        lea     (a3,d3.w),a1            /* the map row */
        lea     tile_attr_table,a2
        move.w  d0,(a6)+
        move.w  #64,(a6)+
        move.w  d6,(a6)+
        move.w  d6,d1
        move.w  d2,d3
1:      moveq   #0,d0
        move.b  (a5,d3.w),d0
        lsl.w   #2,d0
        move.w  (a1,d0.w),d0
        and.w   #0x3FFF,d0
        move.w  d0,(a6)+
        addq.w  #1,d3
        dbra    d1,1b
        move.w  a0,d0
        addq.w  #1,d0                   /* attribute words */
        move.w  d0,(a6)+
        move.w  #64,(a6)+
        move.w  d6,(a6)+
        move.w  d6,d1
        move.w  d2,d3
2:      moveq   #0,d0
        move.b  (a5,d3.w),d0
        lsl.w   #2,d0
        move.b  3(a1,d0.w),d0
        and.w   #0xFF,d0
        add.w   d0,d0
        move.w  (a2,d0.w),(a6)+
        addq.w  #1,d3
        dbra    d1,2b
        movem.l (sp)+,d1-d3/d6
        rts

* ------------------------------------------------------------------------------------------
* vram_store: d0.l = tile pair (code, attr), a0 = shadow VRAM address (0x100000-0x103FFF).
* Stores the pair and, if the map column is shown by a column sprite, queues the cell for its SCB1 and
* keeps the companion (HI/BG) column in step.  Called by the arcade code (main loop and IRQ), so the
* queue is appended with interrupts masked.  Trashes d0-d2/a0-a2.
* ------------------------------------------------------------------------------------------
vram_store:
        cmp.l   (a0),d0
        beq     vs_same                 /* same pair: nothing changes */
        /* fast path: a map column that no column sprite shows only needs the shadow write (check and
           write with the VBlank masked: a column load in between would read the old pair) */
        move    sr,-(sp)
        ori.w   #0x0700,sr
        move.l  a0,d1
        lea     rt_col_loaded,a1
        move.w  rt_c0,d2
        btst    #13,d1
        beq.s   1f
        lea     24(a1),a1
        move.w  rt_c0+2,d2
1:      lsr.w   #2,d1
        and.w   #63,d1                  /* col */
        sub.w   d2,d1
        and.w   #63,d1                  /* j = (col - c0) & 63 */
        cmpi.w  #NCOLSPR,d1
        bcc.s   2f                      /* not in the window */
        add.w   d1,d2
        lea     mod_ncol,a2
        move.b  (a2,d2.w),d2
        and.w   #0xFF,d2                /* s */
        move.l  a0,d1
        lsr.w   #2,d1
        and.w   #63,d1
        cmp.b   (a1,d2.w),d1
        beq.s   vs_slow                 /* shown: queue it */
2:      move.l  d0,(a0)
        move    (sp)+,sr
        rts
vs_slow:                                /* (sr saved at 18(sp), interrupts masked; d2.w = s) */
        move.l  a6,-(sp)                /* 14(sp) */
        movea.l rt_vcmd_ptr,a6
        cmpa.l  #VCMD_BUF,a6
        bcc.s   1f
        lea     VCMD_BUF,a6
1:      bsr     vc_room
        move.l  d0,-(sp)                /* new pair (8(sp) once the col/index words are pushed) */
        move.l  (a0),-(sp)              /* old pair (4(sp)) */
        move.l  d0,(a0)
        move.w  a0,d1
        movea.w #SPR_LAYER0,a2          /* a2.w = sprite base of the layer */
        btst    #13,d1
        beq.s   1f
        movea.w #SPR_LAYER1,a2
1:      and.w   #0x1FFF,d1
        lsr.w   #2,d1                   /* tile index */
        move.w  d1,-(sp)                /* 2(sp): tile index: col = low 6 bits, row = bits 6-10 */
        and.w   #63,d1                  /* col */
        move.w  d1,-(sp)                /* 0(sp): col */
        movea.w d2,a0                   /* s */
        /* mark the cell (cells_flush writes it with the other marked cells of its row) */
        move.w  2(sp),d1
        lsr.w   #6,d1                   /* row */
        move.l  d3,-(sp)
        lea     CELL_ROWS,a1
        cmp.w   #SPR_LAYER0,a2
        beq.s   1f
        addq.l  #4,a1
1:      move.l  (a1),d3
        bset    d1,d3
        move.l  d3,(a1)
        lea     CELL_MASK,a1
        cmp.w   #SPR_LAYER0,a2
        beq.s   2f
        lea     128(a1),a1
2:      lsl.w   #2,d1
        adda.w  d1,a1
        move.l  (a1),d3
        bset    d2,d3
        move.l  d3,(a1)
        move.l  (sp)+,d3
        move.l  d0,d1
        swap    d1
        and.w   #0x3FFF,d1              /* tile */
        /* a non-empty cell past the rows the column shows: show more (positions queued next frame) */
        lea     pen0_table,a1
        btst    #1,(a1,d1.w)
        bne.s   7f
        move.w  2(sp),d1
        lsr.w   #6,d1
        addq.w  #1,d1                   /* row + 1 */
        move.w  a0,d2                   /* s */
        lea     rt_lu_force,a1
        cmp.w   #SPR_LAYER0,a2
        beq.s   6f
        add.w   #24,d2
        addq.l  #1,a1
6:      move.l  a1,-(sp)
        lea     LAY_H,a1
        cmp.b   (a1,d2.w),d1
        bls.s   8f
        move.b  d1,(a1,d2.w)
        movea.l (sp),a1
        st      (a1)
8:      addq.l  #4,sp
7:      and.w   #0xFF,d0
        add.w   d0,d0
        lea     tile_attr_table,a1
        cmp.w   #SPR_LAYER0,a2
        bne     vs_layer1
        /* layer 0 (stack: 0 col, 2 tile index, 4 old pair, 8 new pair): a cell changing category marks the
           HI column dirty (recounted next frame); a category-1 cell that stays so is updated in place */
        move.w  10(sp),d1               /* new attribute */
        move.w  6(sp),d2                /* old attribute */
        eor.w   d1,d2
        btst    #5,d2
        bne.s   vs_hi_dirty
        btst    #5,d1
        beq     vs_out
        move.w  a0,d2                   /* s */
        lea     rt_hi_cnt,a2
        tst.b   (a2,d2.w)
        beq.s   vs_hi_dirty
        move.w  2(sp),d1
        lsr.w   #6,d1                   /* row */
        lea     CMP_HI_R0,a2
        sub.b   (a2,d2.w),d1
        and.b   #31,d1                  /* the range may wrap past row 31 */
        lea     CMP_HI_H,a2
        cmp.b   (a2,d2.w),d1
        bcc.s   vs_hi_dirty
        and.w   #0xFF,d1
        add.w   d1,d1
        add.w   #SPR_HI,d2
        lsl.w   #6,d2
        add.w   d1,d2
        move.w  d2,(a6)+
        move.w  #1,(a6)+
        move.w  #1,(a6)+
        move.l  8(sp),d1
        swap    d1
        and.w   #0x3FFF,d1
        move.w  d1,(a6)+
        move.w  (a1,d0.w),(a6)+
        bra     vs_out
vs_hi_dirty:
        lea     CMP_HI_DIRTY,a2
        move.w  a0,d2
        st      (a2,d2.w)
        st      rt_cmp_any
        bra     vs_out
vs_layer1:
        /* layer 1: a cell changing significance (pen-0 heavy tile with a non-black colour 0) marks the BG
           column dirty; a significant cell that stays so gets its colour updated in place */
        move.l  8(sp),d0
        bsr     bg_sig
        move.w  d0,d2                   /* new */
        move.l  4(sp),d0
        bsr     bg_sig                  /* old */
        cmp.w   d0,d2
        bne.s   vs_bg_dirty
        tst.w   d2
        beq     vs_out
        move.w  a0,d2
        lea     rt_bg_cnt,a2
        tst.b   (a2,d2.w)
        beq.s   vs_bg_dirty
        move.w  2(sp),d1
        lsr.w   #6,d1
        lea     CMP_BG_R0,a2
        sub.b   (a2,d2.w),d1
        bcs.s   vs_bg_dirty
        lea     CMP_BG_H,a2
        cmp.b   (a2,d2.w),d1
        bcc.s   vs_bg_dirty
        and.w   #0xFF,d1
        add.w   d1,d1
        add.w   #SPR_BG,d2
        lsl.w   #6,d2
        add.w   d1,d2
        move.w  d2,(a6)+
        move.w  #1,(a6)+
        move.w  #1,(a6)+
        move.w  #SOLID_TILE,(a6)+
        move.w  10(sp),d1
        and.w   #0x1F,d1
        add.w   #PAL_BG,d1
        lsl.w   #8,d1
        move.w  d1,(a6)+
        bra.s   vs_out
vs_bg_dirty:
        lea     CMP_BG_DIRTY,a2
        move.w  a0,d2
        st      (a2,d2.w)
        st      rt_cmp_any+1
vs_out:
        lea     12(sp),sp
        move.l  a6,rt_vcmd_ptr
        movea.l (sp)+,a6
        move    (sp)+,sr
vs_same:
        rts

* bg_sig: d0.l = tile pair -> d0.w = 1 if the cell needs an opaque backdrop (tile with pen-0 pixels and
* palette colour 0 not black), else 0.  Trashes d1; a1 preserved.
bg_sig:
        move.l  a1,-(sp)
        move.w  d0,d1
        and.w   #0x1F,d1
        lsl.w   #5,d1
        lea     ARC_PAL,a1
        cmpi.w  #0x8000,(a1,d1.w)
        beq.s   1f
        swap    d0
        and.w   #0x3FFF,d0
        lea     pen0_table,a1
        move.b  (a1,d0.w),d0
        and.w   #1,d0
        movea.l (sp)+,a1
        rts
1:      moveq   #0,d0
        movea.l (sp)+,a1
        rts

* bg_recount: rt_bg_cnt (and the BG column uploads) for every loaded layer-1 column, after a tile palette
* changed whether its colour 0 is black.  Trashes d0-d1/a5 (a6 = command queue).
bg_recount:
        lea     rt_col_loaded+24,a5
        moveq   #0,d0                   /* s */
1:      move.b  (a5,d0.w),d1
        cmpi.b  #0xFF,d1
        beq.s   2f
        and.w   #0x3F,d1
        bsr     bg_count
2:      addq.w  #1,d0
        cmpi.w  #NCOLSPR,d0
        bcs.s   1b
        st      rt_lu_force+1
        rts

* ------------------------------------------------------------------------------------------
* spr_lists (jmp at 0x44E6, in the arcade interrupt): the arcade copies its three sprite lists (built in
* the previous interrupt) to sprite RAM; they are converted straight into the field arrays of the two
* pools instead (same entries and order as sprites_update over the copy).  With more high-priority
* sprites than pool 1 holds, the original copy runs and video_frame calls sprites_slow on it.
* Keeps every register but a0/a1/d5/d6, like the original.
* ------------------------------------------------------------------------------------------
        .global spr_lists
spr_lists:
        movem.l d0-d4/d7/a2-a6,-(sp)
        st      rt_spr_busy
        st      rt_spr_conv
        clr.w   rt_eff_n
        clr.w   rt_eff_nt
        lea     spr_y_table,a5
        lea     spr_x_table,a6
        lea     spr_col_table,a3
        btst    #0,ARC_WRAM+0x19B       /* night stage (the game loads its colours into palette bank 7) */
        beq.s   1f
        lea     spr_col_table_night,a3
1:      movea.l rt_set_base,a1
        lea     SPR_F1_OFS(a1),a2
        move.w  #SPR_OBJ_MAX,d7         /* free slots, pool 0 */
        moveq   #SPR_PRI1_MAX,d1        /* free slots, pool 1 */
        movea.w #(0x1000-6)/8,a4        /* entries the sprite RAM holds */
        lea     ARC_WRAM+0x1600,a0
        move.w  ARC_WRAM+0x27E,d6
        bsr     sl_seg
        bne     sl_slow
        lea     ARC_WRAM+0x1DF8,a0
        move.w  ARC_WRAM+0x280,d6
        bsr     sl_seg
        bne     sl_slow
        lea     ARC_WRAM+0x25C8,a0
        move.w  ARC_WRAM+0x282,d6
        add.w   ARC_WRAM+0x284,d6
        bsr     sl_seg
        bne     sl_slow
        move.w  #SPR_OBJ_MAX,d0
        sub.w   d7,d0
        move.w  d0,rt_spr_n0
        moveq   #SPR_PRI1_MAX,d0
        sub.w   d1,d0
        move.w  d0,rt_spr_n1
        move.w  ARC_WRAM+0x27E,d0       /* the copy's count, as the original keeps it */
        add.w   ARC_WRAM+0x280,d0
        add.w   ARC_WRAM+0x282,d0
        add.w   ARC_WRAM+0x284,d0
        move.w  d0,ARC_WRAM+0x27C
        clr.b   rt_spr_busy
        movem.l (sp)+,d0-d4/d7/a2-a6
        rts
sl_slow:
        st      rt_spr_slow             /* (rt_spr_busy stays set until sprites_slow has refilled the pools) */
        clr.w   rt_eff_n                /* the copy below overwrites the column buffers (ARC_SPR) of both */
        clr.w   rt_set_cnt+4            /* sets: none shown from the sealed frames either */
        clr.w   rt_set_cnt+8+4
        movem.l (sp)+,d0-d4/d7/a2-a6
        lea     ARC_SPR+6,a1            /* the original routine from its second instruction */
        jmp     0x44EC

* sl_seg: a0 = list, d6 = entries, a4 = entries the sprite RAM still holds (decremented): converts them.
* Z set when done, Z clear when pool 1 overflowed.
sl_seg:
        move.w  a4,d0
        cmp.w   d0,d6
        bls.s   1f
        move.w  d0,d6
1:      suba.w  d6,a4
        subq.w  #1,d6
        bmi.s   sl_done
sl_loop:
        moveq   #0,d4
        move.b  1(a0),d4                /* y byte */
        add.w   d4,d4
        move.w  (a5,d4.w),d4            /* SCB3 (y | height 1), 0 = off screen */
        beq.s   sl_next
        move.w  4(a0),d5                /* w2: x, colour */
        and.w   #0x3FF,d5
        add.w   d5,d5
        move.w  (a6,d5.w),d5            /* SCB4 + 1, 0 = off screen */
        beq.s   sl_next
        moveq   #0x7C,d2
        and.b   4(a0),d2                /* colour * 4 */
        move.l  (a3,d2.w),d2            /* palette << 8 : tile OR mask */
        move.w  (a0),d0
        rol.w   #2,d0                   /* w0 bits 15,14 -> 1,0 (vflip, hflip) */
        and.w   #3,d0
        move.w  6(a0),d3
        and.w   #0x3FFF,d3
        tst.w   d2
        bmi     sl_eff                  /* night: headlight tile (light tile set, OR mask 0x8000) */
        cmpi.w  #0x3700,d3
        bcc.s   sl_hi
        or.w    d2,d3
        swap    d2
        or.w    d0,d2
        subq.w  #1,d7
        bmi.s   sl_full0
        move.w  d3,(a1)
        move.w  d2,SPR_F0_SZ(a1)
        move.w  d4,2*SPR_F0_SZ(a1)
        move.w  d5,3*SPR_F0_SZ(a1)
        addq.l  #2,a1
sl_next:
        addq.l  #8,a0
        dbra    d6,sl_loop
sl_done:
        moveq   #0,d0                   /* Z set */
        rts
sl_full0:
        addq.w  #1,d7                   /* pool 0 full: the sprite is dropped */
        bra.s   sl_next
sl_hi:
        or.w    d2,d3
        swap    d2
        or.w    d0,d2
        subq.w  #1,d1
        bmi.s   1f
        move.w  d3,(a2)
        move.w  d2,SPR_F1_SZ(a2)
        move.w  d4,2*SPR_F1_SZ(a2)
        move.w  d5,3*SPR_F1_SZ(a2)
        addq.l  #2,a2
        bra.s   sl_next
1:      moveq   #1,d0                   /* Z clear: pool 1 overflow */
        rts

* sl_eff: a headlight tile of a night stage (d3 code, d2 = palette<<8 : 0x8000, d0 flips, d4 SCB3 with
* height 1, d5 SCB4).  The arcade draws its beams as grids of 16x16 tiles, column by column, downwards:
* a tile right below the previous one (same x and attribute) extends that column - one Neo Geo sprite
* up to 16 tiles tall instead of one sprite per tile (there are about 250 of them, more than the pool and
* than the 96 sprites a scanline can show).
sl_eff:
        or.w    d2,d3                   /* tile */
        swap    d2
        or.w    d0,d2                   /* attribute */
        move.l  a1,-(sp)
        lea     RT_VARS,a1              /* the variables below, addressed off a1 (shorter than absolute) */
        .equ    E_CP,   rt_eff_cp-RT_VARS
        .equ    E_TP,   rt_eff_tp-RT_VARS
        .equ    E_BASE, rt_eff_base-RT_VARS
        .equ    E_N,    rt_eff_n-RT_VARS
        .equ    E_NT,   rt_eff_nt-RT_VARS
        .equ    E_LAST, rt_eff_last-RT_VARS
        cmpi.w  #EFF_TILES_MAX,E_NT(a1)
        bcc     9f                      /* no room left: dropped */
        tst.w   E_N(a1)
        beq.s   4f                      /* first column of the frame */
        cmp.w   E_LAST(a1),d5           /* same x */
        bne.s   2f
        cmp.w   E_LAST+2(a1),d2         /* same attribute */
        bne.s   2f
        move.w  E_LAST+4(a1),d0
        sub.w   #0x800,d0               /* 16 lines lower */
        cmp.w   d0,d4
        bne.s   2f
        movea.l E_CP(a1),a1             /* current column */
        cmpi.w  #16,6(a1)
        bcc.s   1f
        addq.w  #1,6(a1)
        addq.w  #1,(a1)                 /* height + 1 */
        lea     RT_VARS,a1
        move.w  d4,E_LAST+4(a1)
        bra.s   6f
4:      move.l  E_BASE(a1),d0
        move.l  d0,E_CP(a1)
        addi.l  #EFF_TILES_OFS,d0
        move.l  d0,E_TP(a1)
        bra.s   5f
1:      lea     RT_VARS,a1              /* column full (16 tiles): a new one */
2:      cmpi.w  #EFF_MAX,E_N(a1)        /* a new column */
        bcc.s   9f
        addq.l  #8,E_CP(a1)
5:      move.w  E_NT(a1),d0
        movea.l E_CP(a1),a1
        move.w  d4,(a1)+
        move.w  d5,(a1)+
        move.w  d0,(a1)+
        move.w  #1,(a1)
        lea     RT_VARS,a1
        addq.w  #1,E_N(a1)
        move.w  d5,E_LAST(a1)
        move.w  d2,E_LAST+2(a1)
        move.w  d4,E_LAST+4(a1)
6:      addq.w  #1,E_NT(a1)             /* the tile */
        addq.l  #4,E_TP(a1)
        movea.l E_TP(a1),a1
        move.w  d3,-4(a1)
        move.w  d2,-2(a1)
9:      movea.l (sp)+,a1
        bra     sl_next

* ------------------------------------------------------------------------------------------
* sprites_slow: arcade sprite list (copy in ARC_SPR) -> the field arrays of the two pools (SPR_F0/SPR_F1),
* when it holds more high-priority sprites (codes >= 0x3700: HUD, texts, title logos; MAME's priority rule)
* than pool 1: one pass per pool, and the first of those entries go to pool 0 after its own sprites so
* that the list order (later entries on top) is kept.
* ------------------------------------------------------------------------------------------
sprites_slow:
        move.l  a6,-(sp)
        /* entries written this frame by the arcade copy routine 0x44E6: the list counters */
        move.w  ARC_WRAM+0x27E,d6
        add.w   ARC_WRAM+0x280,d6
        add.w   ARC_WRAM+0x282,d6
        add.w   ARC_WRAM+0x284,d6
        cmpi.w  #(0x1000-6)/8,d6
        bls.s   1f
        move.w  #(0x1000-6)/8,d6
1:      subq.w  #1,d6
        move.w  d6,-(sp)                /* entries - 1 (dbra counter) */
        lea     spr_y_table,a5
        lea     spr_x_table,a6
        /* pool 0 */
        movea.l rt_set_base,a1
        lea     SPR_F0_SZ(a1),a2
        lea     SPR_F0_SZ(a2),a3
        lea     SPR_F0_SZ(a3),a4
        move.w  #SPR_OBJ_MAX,d7         /* free slots */
        lea     ARC_SPR+6,a0
        move.w  (sp),d6
        moveq   #0,d0                   /* select: code < 0x3700 */
        bsr     su_pass
        move.w  #SPR_OBJ_MAX,d0
        sub.w   d7,d0
        move.w  d0,rt_spr_n0
        /* hi entries (codes >= 0x3700) beyond what pool 1 holds: the first ones of the list go to pool 0 after
           its own sprites, so that the list order (later entries on top) is kept (big title logos) */
        lea     ARC_SPR+6,a0
        moveq   #-SPR_PRI1_MAX,d1
        move.w  (sp),d6
        bmi.s   5f                      /* empty list */
1:      move.w  6(a0),d3
        and.w   #0x3FFF,d3
        cmpi.w  #0x3700,d3
        bcs.s   2f
        addq.w  #1,d1
2:      addq.l  #8,a0
        dbra    d6,1b
        lea     ARC_SPR+6,a0
        move.w  (sp),d6
        tst.w   d1
        ble.s   5f
        move.w  rt_spr_n0,d0
        move.w  #SPR_OBJ_MAX,d7
        sub.w   d0,d7
        add.w   d0,d0
        movea.l rt_set_base,a1
        adda.w  d0,a1
        lea     SPR_F0_SZ(a1),a2
        lea     SPR_F0_SZ(a2),a3
        lea     SPR_F0_SZ(a3),a4
        move.w  d7,-(sp)
3:      move.w  6(a0),d3
        and.w   #0x3FFF,d3
        cmpi.w  #0x3700,d3
        bcs.s   4f
        tst.w   d7
        beq.s   6f
        bsr     su_conv
        bne.s   6f
        subq.w  #1,d7
6:      subq.w  #1,d1
        bne.s   4f
        addq.l  #8,a0                   /* K entries done: pool 1 starts at the next one */
        subq.w  #1,d6
        bra.s   7f
4:      addq.l  #8,a0
        dbra    d6,3b
7:      move.w  (sp)+,d0
        sub.w   d7,d0
        add.w   d0,rt_spr_n0
5:      /* pool 1 */
        movea.l rt_set_base,a1
        lea     SPR_F1_OFS(a1),a1
        lea     SPR_F1_SZ(a1),a2
        lea     SPR_F1_SZ(a2),a3
        lea     SPR_F1_SZ(a3),a4
        move.w  #SPR_PRI1_MAX,d7
        moveq   #-1,d0                  /* select: code >= 0x3700 */
        bsr     su_pass
        move.w  #SPR_PRI1_MAX,d0
        sub.w   d7,d0
        move.w  d0,rt_spr_n1
        addq.l  #2,sp
        movea.l (sp)+,a6
        clr.b   rt_spr_busy
        rts

* su_pass: a0 = first entry, d6 = entries left - 1, d0 = 0 (codes < 0x3700) or -1 (>= 0x3700),
* a1-a4 = field pointers, d7 = free slots (decremented), a5/a6 = y/x tables.  Returns when the list
* ends (d6 = -1) or the pool is full (d7 = 0; a0/d6 then designate the next entry).  Trashes d0-d5.
su_pass:
        tst.w   d6
        bmi     su_end
        tst.w   d0
        bne     su_hi
su_lo:  /* ---- codes below 0x3700 ---- */
        move.w  6(a0),d3                /* code */
        and.w   #0x3FFF,d3
        cmpi.w  #0x3700,d3
        bcc.s   su_lo_next
        bsr.s   su_conv
        bne.s   su_lo_next              /* off screen */
        subq.w  #1,d7
        beq.s   su_full
su_lo_next:
        addq.l  #8,a0
        dbra    d6,su_lo
        rts
su_hi:  /* ---- codes 0x3700 and above ---- */
        move.w  6(a0),d3
        and.w   #0x3FFF,d3
        cmpi.w  #0x3700,d3
        bcs.s   su_hi_next
        bsr.s   su_conv
        bne.s   su_hi_next
        subq.w  #1,d7
        beq.s   su_full
su_hi_next:
        addq.l  #8,a0
        dbra    d6,su_hi
        rts
su_full:
        addq.l  #8,a0
        subq.w  #1,d6
su_end: rts

* su_conv: a0 -> arcade entry (w0 y/flips, w1, w2 x/colour, w3 code), d3 = code: append the Neo Geo
* sprite to the fields (a1 tile, a2 attr, a3 SCB3, a4 SCB4).  Z clear (bne) when it is off screen.
su_conv:
        moveq   #0,d4
        move.b  1(a0),d4                /* y byte */
        add.w   d4,d4
        move.w  (a5,d4.w),d4            /* SCB3 (y | height 1), 0 = off screen */
        beq.s   9f
        move.w  4(a0),d2                /* w2 */
        move.w  d2,d5
        and.w   #0x3FF,d5
        add.w   d5,d5
        move.w  (a6,d5.w),d5            /* SCB4 + 1, 0 = off screen */
        beq.s   8f
        and.w   #0x7C00,d2              /* colour << 10 */
        cmpi.w  #0x4000,d2
        bcc.s   1f
        lsr.w   #2,d2                   /* colour << 8 */
        add.w   #PAL_SPRITES<<8,d2
        bra.s   2f
1:      btst    #0,ARC_WRAM+0x19B
        bne.s   3f
        move.w  #PAL_SHADOW<<8,d2       /* shadow: dithered tile set (gfx2crom --shadow) */
        or.w    #0x4000,d3
        bra.s   2f
3:      move.w  #PAL_LIGHT<<8,d2        /* night: light tile set (gfx2crom --light) */
        or.w    #0x8000,d3
2:      move.w  (a0),d0
        rol.w   #2,d0                   /* w0 bits 15,14 -> 1,0 (vflip, hflip) */
        and.w   #3,d0
        or.w    d0,d2
        move.w  d3,(a1)+
        move.w  d2,(a2)+
        move.w  d4,(a3)+
        move.w  d5,(a4)+
        moveq   #0,d0                   /* Z set: taken */
        rts
8:
9:      moveq   #1,d0                   /* Z clear: skipped */
        rts

        .section .rodata
* ------------------------------------------------------------------------------------------
* Tables  (NB: in m68k gas '|' starts a comment, so expressions use '+' for disjoint bit fields)
* ------------------------------------------------------------------------------------------
* tile_attr_table: low byte of the arcade tile attribute word -> Neo Geo SCB1 attribute word
*   arcade: bits 0-4 colour, 5 priority, 6 flip x, 7 flip y ; neo: bits 15-8 palette, 1 vflip, 0 hflip
        .align  2
tile_attr_table:
        .set    i,0
        .rept   256
        .word   (((i & 0x1F) + PAL_TILES) << 8) + ((i >> 6) & 3)
        .set    i,i+1
        .endr

* spr_y_table: arcade sprite y byte -> SCB3 word (y | height 1), 0 when off the Neo Geo screen
spr_y_table:
        .set    i,0
        .rept   256
        .set    ny, ((240 - i) & 0xFF) - CROP_Y
        .if (ny >= -15) && (ny <= 223)
        .word   (((496 - ny) & 0x1FF) << 7) + 1
        .else
        .word   0
        .endif
        .set    i,i+1
        .endr

* spr_x_table: arcade sprite x (10 bits) -> SCB4 word (x scaled by 7/8) + 1 (the unused bit 0, so that the
* word is never 0), 0 when off screen
spr_x_table:
        .set    i,0
        .rept   1024
        .set    nx, (((i - 15 - CROP_X + 512) * 7) / 8) - 448      /* x 7/8: sprites are shrunk to 14 pixels */
        .if (nx >= -14) && (nx <= 319)
        .word   ((nx & 0x1FF) << 7) + 1
        .else
        .word   0
        .endif
        .set    i,i+1
        .endr

* spr_col_table: arcade sprite colour (5 bits) -> high word: palette << 8, low word: tile OR mask
* (colours 16-31: the colour-effect sprites, drawn with the shadow tile set; spr_col_table_night: with the
* light tile set and palette, for the night stages)
spr_col_table:
        .set    i,0
        .rept   32
        .if i < 16
        .long   (PAL_SPRITES + i) << 24
        .else
        .long   (PAL_SHADOW << 24) + 0x4000
        .endif
        .set    i,i+1
        .endr
spr_col_table_night:
        .set    i,0
        .rept   32
        .if i < 16
        .long   (PAL_SPRITES + i) << 24
        .else
        .long   (PAL_LIGHT << 24) + 0x8000
        .endif
        .set    i,i+1
        .endr

* light_pal: palette PAL_LIGHT, pen p = p-8 steps brighter (lit ground: a dim beige, brighter for higher pens)
light_pal:
        .word   0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x0553,0x0654,0x0764,0x0875,0x0986,0x0A97,0x0BA8

* pen0_table: one byte per arcade tile, 1 if it has at least 64 pen-0 pixels (gfx2crom.py --pen0)
pen0_table:
        .incbin "pen0.bin"

* mod_ncol: i mod NCOLSPR for i < 256 (c0 + window column < 192 + NCOLSPR)
mod_ncol:
        .set    i,0
        .rept   256
        .byte   i % NCOLSPR
        .set    i,i+1
        .endr
        .align  2

* pal_table: arcade xBRG444 (12 bits) -> Neo Geo colour word (4->5 bit expansion c5 = c4<<1 | c4>>3)
pal_table:
        .set    i,0
        .rept   4096
        .word   (((i >> 4) & 0xF) << 8) + (((i >> 7) & 1) << 14) + ((i & 0xF) << 4) + (((i >> 3) & 1) << 13) + (((i >> 8) & 0xF) << 0) + (((i >> 11) & 1) << 12)
        .set    i,i+1
        .endr
