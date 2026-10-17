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
        .global video_init, video_init_min, video_vblank, video_frame
        .global pal_write, vram_mark_dirty, video_mark_all_dirty, pal_table, vram_store, vram_flush

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
        /* shadow palette: every pen black (the dithered tile set leaves half the pixels transparent) */
        lea     PALETTES+PAL_SHADOW*32,a0
        moveq   #15,d0
1:      move.w  #0x8000,(a0)+
        dbra    d0,1b
        /* column sprites of both layers: height 32 tiles, x off-screen for now */
        move.w  #VRAM_SCB3+SPR_LAYER1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        moveq   #2*NCOLSPR-1,d0
2:      move.w  #0x0020,REG_VRAMRW      /* y=0 (496), not sticky, 32 tiles */
        dbra    d0,2b
        /* no column loaded yet, everything dirty */
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
        move.w  #0xFFFF,rt_lu_force     /* queue the layer positions on the first frame */
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
        move.w  rt_backdrop,PALETTES+255*32+30
        tst.b   rt_pal_any
        beq.s   9f
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
9:      rts

* ------------------------------------------------------------------------------------------
* VRAM command buffer.  Everything the frame changes in VRAM is queued (vc_* helpers, a6 = write pointer
* while video_frame runs) and replayed by vram_flush at the start of the next VBlank, so the screen is
* never modified while the LSPC draws it, and no VRAM address/data sequence can be split by the IRQ.
* ------------------------------------------------------------------------------------------

* vc_room: make sure a block of up to 64 data words fits after a6; otherwise write the queue to VRAM now
* (rare: whole-layer reloads) and restart it.  Preserves all registers except a6.
vc_room:
        cmpa.l  #VCMD_END-VCMD_ROOM,a6
        bcs.s   9f
        movem.l d0-d1/a0-a2,-(sp)
        lea     VCMD_BUF,a0
        movea.l a6,a2
        bsr     vc_play
        lea     VCMD_BUF,a6
        movem.l (sp)+,d0-d1/a0-a2
9:      rts

* vc_play: write the blocks in [a0, a2) to VRAM.  Trashes d0-d1/a0-a1.
vc_play:
        lea     REG_VRAMRW,a1
        cmpa.l  a2,a0
        bcc.s   9f
1:      move.w  (a0)+,d0                /* address */
        move.w  (a0)+,REG_VRAMMOD
        move.w  (a0)+,d1                /* count - 1 */
        move.w  d0,REG_VRAMADDR         /* (>= 16 cycles after the previous data write) */
2:      move.w  (a0)+,(a1)              /* 22 cycles per word: >= 12 between data writes */
        dbra    d1,2b
        cmpa.l  a2,a0
        bcs.s   1b
9:      rts

* ------------------------------------------------------------------------------------------
* vram_flush: called at the very start of the VBlank handler.  Replays the command queue, then writes
* the two sprite pools prepared by sprites_update and hides the sprites no longer used.
* Trashes d0-d5/a0-a3.
* ------------------------------------------------------------------------------------------
vram_flush:
        lea     VCMD_BUF,a0
        movea.l rt_vcmd_ptr,a2
        cmpa.l  a0,a2
        bls.s   1f                      /* empty (or not initialised yet) */
        bsr     vc_play
1:      move.l  #VCMD_BUF,rt_vcmd_ptr
        lea     REG_VRAMRW,a1
        move.w  #SPR_OBJ,d0
        move.w  rt_spr_n0,d1
        lea     SPR_F0,a0
        move.w  #SPR_F0_SZ,d2
        lea     rt_sprites_used,a2
        bsr.s   vf_pool
        move.w  #SPR_PRI1,d0
        move.w  rt_spr_n1,d1
        lea     SPR_F1,a0
        move.w  #SPR_F1_SZ,d2
        lea     rt_sprites_used1,a2
* vf_pool: d0 = first sprite, d1 = count, a0 = fields (4 arrays of d2 bytes), a1 = REG_VRAMRW,
* a2 -> end of the pool as written last time (updated).
vf_pool:
        move.w  d1,d5
        add.w   d0,d5                   /* new end */
        subq.w  #1,d1
        bmi.s   5f
        move.w  d0,d3
        lsl.w   #6,d3                   /* SCB1 of the first sprite */
        move.w  #64,REG_VRAMMOD
        move.w  d3,REG_VRAMADDR
        movea.l a0,a3
        move.w  d1,d4
1:      move.w  (a3)+,(a1)              /* tiles (SCB1 even words) */
        dbra    d4,1b
        addq.w  #1,d3
        lea     (a0,d2.w),a3
        move.w  d3,REG_VRAMADDR
        move.w  d1,d4
2:      move.w  (a3)+,(a1)              /* attributes (SCB1 odd words) */
        dbra    d4,2b
        move.w  #1,REG_VRAMMOD
        move.w  d0,d3
        add.w   #VRAM_SCB3,d3
        lea     (a0,d2.w),a3
        adda.w  d2,a3
        move.w  d3,REG_VRAMADDR
        move.w  d1,d4
3:      move.w  (a3)+,(a1)              /* SCB3 */
        dbra    d4,3b
        add.w   #VRAM_SCB4-VRAM_SCB3,d3
        lea     (a0,d2.w),a3
        adda.w  d2,a3
        adda.w  d2,a3
        move.w  d3,REG_VRAMADDR
        move.w  d1,d4
4:      move.w  (a3)+,(a1)              /* SCB4 */
        dbra    d4,4b
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
        move.l  a6,rt_vcmd_ptr
        move.w  REG_LSPCMODE,rt_dbg+10  /* profiling */
        bra     sprites_update

* layer_update: d0 = scrollx, d1 = scrolly, a3 = map base (shadow VRAM), a4 = rt_c0 slot of the layer,
*               a5 = loaded-column table, d7 = first Neo Geo sprite of the layer, a6 = command queue.
* Pass 1 (re)loads the column sprites whose map column changed (col_load); pass 2 queues the positions of
* the layer's NCOLSPR sprites and of its companion set (HI for layer 0, BG for layer 1), only when they
* changed (scroll, a reloaded column, a companion column shown or hidden).
layer_update:
        add.w   #CROP_X,d0
        and.w   #0x3FF,d0
        add.w   #CROP_Y,d1
        add.w   #496,d1
        and.w   #0x1FF,d1
        lsl.w   #7,d1
        or.w    #0x20,d1                /* SCB3 value: y | height 32 */
        move.w  d1,d6
        move.w  d0,d2
        and.w   #15,d2                  /* fine scroll */
        neg.w   d2
        movea.w d2,a2                   /* a2 = x of window column 0 */
        move.w  d0,d3
        lsr.w   #4,d3                   /* c0 = first map column (unwrapped, 0..66) */
        move.w  d3,(a4)                 /* rt_c0 of this layer, for vram_store */
        lea     mod_ncol,a0
        moveq   #0,d4
        move.b  (a0,d3.w),d4            /* s0 = sprite of window column 0 */
        lea     tile_attr_table,a1
        /* pass 1: columns whose map column changed */
        move.w  d4,d0                   /* s */
        move.w  d3,d1                   /* unwrapped map column */
        moveq   #NCOLSPR-1,d5
lu_chk:
        move.w  d1,d3
        and.w   #63,d3
        cmp.b   (a5,d0.w),d3
        beq.s   1f
        bsr     col_load                /* d0 = s, d3 = map column */
1:      addq.w  #1,d1
        addq.w  #1,d0
        cmpi.w  #NCOLSPR,d0
        bcs.s   2f
        moveq   #0,d0
2:      dbra    d5,lu_chk
        /* companion columns with cells that changed category: recount them */
        lea     CMP_HI_DIRTY,a0
        cmpi.w  #SPR_LAYER0,d7
        beq.s   1f
        lea     CMP_BG_DIRTY,a0
1:      moveq   #0,d0
2:      tst.b   (a0,d0.w)
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
        /* pass 2: skip when nothing moved (same x0, same y, no forced update) */
        lea     rt_lu_sig,a0
        lea     rt_lu_force,a4
        cmpi.w  #SPR_LAYER0,d7
        beq.s   3f
        addq.l  #4,a0
        addq.l  #1,a4
3:      move.w  a2,d0
        swap    d0
        move.w  d6,d0                   /* d0 = x0:y signature */
        tst.b   (a4)
        bne.s   4f
        cmp.l   (a0),d0
        beq     lu_done
4:      move.l  d0,(a0)
        clr.b   (a4)
        /* x of sprite s = x0 + 16 * ((s - s0) mod NCOLSPR) -> rt_xbuf */
        moveq   #NCOLSPR,d3
        sub.w   d4,d3
        cmpi.w  #NCOLSPR,d3
        bcs.s   1f
        moveq   #0,d3
1:      lsl.w   #4,d3
        add.w   a2,d3                   /* x of s = 0 */
        lea     rt_xbuf,a0
        moveq   #0,d0
        moveq   #NCOLSPR-1,d5
lu_x:   cmp.w   d4,d0
        bne.s   2f
        move.w  a2,d3                   /* s0 shows window column 0 */
2:      move.w  d3,d1
        and.w   #0x1FF,d1
        lsl.w   #7,d1
        move.w  d1,(a0)+
        add.w   #16,d3
        addq.w  #1,d0
        dbra    d5,lu_x
        /* SCB4 of the layer's sprites and of the companion set */
        bsr     vc_room
        move.w  d7,d0
        bsr     lu_scb4
        move.w  #SPR_HI,d3
        cmpi.w  #SPR_LAYER0,d7
        beq.s   3f
        move.w  #SPR_BG,d3
3:      move.w  d3,d0                   /* companion base */
        bsr     lu_scb4
        /* SCB3: the layer: y | 32 for every column */
        move.w  d7,d0
        add.w   #VRAM_SCB3,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #NCOLSPR-1,(a6)+
        moveq   #NCOLSPR-1,d5
4:      move.w  d6,(a6)+
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
        bra.s   lu_done
6:      move.w  d1,(a6)+
        addq.l  #1,a1
        addq.l  #1,a4
        dbra    d5,5b
lu_done:
        lea     tile_attr_table,a1
        rts

* lu_scb4: d0 = first sprite of a set: queue SCB4 of its NCOLSPR sprites from rt_xbuf.  Trashes d0/d5/a1.
lu_scb4:
        add.w   #VRAM_SCB4,d0
        move.w  d0,(a6)+
        move.w  #1,(a6)+
        move.w  #NCOLSPR-1,(a6)+
        lea     rt_xbuf,a1
        moveq   #NCOLSPR-1,d5
1:      move.w  (a1)+,(a6)+
        dbra    d5,1b
        lea     tile_attr_table,a1      /* (callers expect a1 = tile_attr_table) */
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
        moveq   #31,d2
4:      move.l  (a0),d4
        swap    d4
        and.w   #0x3FFF,d4
        move.w  d4,(a6)+
        moveq   #0,d4
        move.b  3(a0),d4
        add.w   d4,d4
        move.w  (a1,d4.w),(a6)+
        lea     256(a0),a0
        dbra    d2,4b
        move.w  d5,d0
        cmpi.w  #SPR_LAYER0,d7
        bne.s   1f
        bsr     hi_count
        bra.s   2f
1:      bsr     bg_count
2:      movem.l (sp)+,d0-d6/a0/a2
        rts

* hi_count: d0 = s, d1 = map column (layer 0): rt_hi_cnt[s] = category-1 cells, CMP_HI_R0/H[s] = their row
* range; queues the HI column (those rows only) when it has some.  Forces the positions to be queued again.
* Preserves registers except a6.
hi_count:
        movem.l d0-d5/a0-a1,-(sp)
        move.w  d1,d2
        lsl.w   #2,d2
        lea     ARC_VRAM,a0
        adda.w  d2,a0
        moveq   #0,d3                   /* count */
        moveq   #-1,d4                  /* first row */
        moveq   #0,d5                   /* last row */
        moveq   #0,d2                   /* row */
1:      btst    #5,3(a0)
        beq.s   2f
        addq.w  #1,d3
        tst.w   d4
        bpl.s   3f
        move.w  d2,d4
3:      move.w  d2,d5
2:      lea     256(a0),a0
        addq.w  #1,d2
        cmpi.w  #32,d2
        bcs.s   1b
        lea     rt_hi_cnt,a0
        move.b  d3,(a0,d0.w)
        st      rt_lu_force
        tst.w   d3
        beq.s   4f
        lea     CMP_HI_R0,a0
        move.b  d4,(a0,d0.w)
        sub.w   d4,d5
        addq.w  #1,d5
        lea     CMP_HI_H,a0
        move.b  d5,(a0,d0.w)
        bsr     hi_upload
4:      movem.l (sp)+,d0-d5/a0-a1
        rts

* hi_upload: d0 = s, d1 = map column: queue SCB1 of HI sprite SPR_HI+s = rows r0..r0+h-1 of the column
* (category-1 tiles, HI_BLANK for the others).  Preserves registers except a6.
hi_upload:
        movem.l d0-d4/a0-a1,-(sp)
        bsr     vc_room
        lea     CMP_HI_R0,a0
        moveq   #0,d3
        move.b  (a0,d0.w),d3
        lea     CMP_HI_H,a0
        moveq   #0,d2
        move.b  (a0,d0.w),d2
        move.w  d1,d4
        lsl.w   #2,d4
        lea     ARC_VRAM,a0
        adda.w  d4,a0
        lsl.w   #8,d3
        adda.w  d3,a0                   /* first row */
        lea     tile_attr_table,a1
        add.w   #SPR_HI,d0
        lsl.w   #6,d0
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
        dbra    d2,1b
        movem.l (sp)+,d0-d4/a0-a1
        rts

* bg_count: d0 = s, d1 = map column (layer 1): rt_bg_cnt[s] = cells needing the backdrop, CMP_BG_R0/H[s] =
* their row range; queues the BG column (those rows) when there are some.  Preserves registers except a6.
bg_count:
        movem.l d0-d6/a0-a1,-(sp)
        movea.w d1,a1                   /* keep the column (bg_sig trashes d1) */
        move.w  d1,d2
        lsl.w   #2,d2
        lea     ARC_VRAM+0x2000,a0
        adda.w  d2,a0
        move.w  d0,d6                   /* s */
        moveq   #0,d3                   /* count */
        moveq   #-1,d4                  /* first row */
        moveq   #0,d5                   /* last row */
        moveq   #0,d2                   /* row */
1:      move.l  (a0),d0
        bsr     bg_sig
        beq.s   2f
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
4:      movem.l (sp)+,d0-d6/a0-a1
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

* ------------------------------------------------------------------------------------------
* vram_store: d0.l = tile pair (code, attr), a0 = shadow VRAM address (0x100000-0x103FFF).
* Stores the pair and, if the map column is shown by a column sprite, queues the cell for its SCB1 and
* keeps the companion (HI/BG) column in step.  Called by the arcade code (main loop and IRQ), so the
* queue is appended with interrupts masked.  Trashes d0-d2/a0-a2.
* ------------------------------------------------------------------------------------------
vram_store:
        addq.w  #1,rt_dbg+14            /* profiling: calls per frame */
        cmp.l   (a0),d0
        beq     vs_same                 /* same pair: nothing changes */
        /* fast path: a map column that no column sprite shows only needs the shadow write */
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
        rts
vs_slow:
        move    sr,-(sp)                /* 18(sp) */
        ori.w   #0x0700,sr
        move.l  a6,-(sp)                /* 14(sp) */
        movea.l rt_vcmd_ptr,a6
        cmpa.l  #VCMD_BUF,a6
        bcc.s   1f
        lea     VCMD_BUF,a6
1:      bsr     vc_room
        move.l  d0,-(sp)                /* new pair (8(sp) once the col/index words are pushed) */
        move.l  (a0),-(sp)              /* old pair (4(sp)) */
        move.l  d0,(a0)
        move.l  a0,d1
        lea     rt_col_loaded,a1
        move.w  rt_c0,d2
        move.w  #SPR_LAYER0,a2          /* a2.w = sprite base of the layer */
        btst    #13,d1
        beq.s   1f
        lea     24(a1),a1
        move.w  rt_c0+2,d2
        move.w  #SPR_LAYER1,a2
1:      and.w   #0x1FFF,d1
        lsr.w   #2,d1                   /* tile index */
        move.w  d1,-(sp)                /* 2(sp): tile index: col = low 6 bits, row = bits 6-10 */
        and.w   #63,d1                  /* col */
        move.w  d1,-(sp)                /* 0(sp): col */
        sub.w   d2,d1
        and.w   #63,d1                  /* j = (col - c0) & 63 */
        cmpi.w  #NCOLSPR,d1
        bcc     vs_out                  /* not in the window */
        add.w   d1,d2                   /* c0 + j (< 128) */
        lea     mod_ncol,a0
        move.b  (a0,d2.w),d2
        and.w   #0xFF,d2                /* s */
        move.w  (sp),d1                 /* col */
        cmp.b   (a1,d2.w),d1
        bne     vs_out                  /* column not loaded yet (will be loaded whole) */
        movea.w d2,a0                   /* s */
        move.w  a2,d1
        add.w   d2,d1
        lsl.w   #6,d1                   /* SCB1 base of sprite s */
        move.w  2(sp),d2
        lsr.w   #6,d2                   /* row */
        add.w   d2,d2
        add.w   d2,d1
        move.w  d1,(a6)+                /* queue the cell: tile, attribute */
        move.w  #1,(a6)+
        move.w  #1,(a6)+
        move.l  d0,d1
        swap    d1
        and.w   #0x3FFF,d1
        move.w  d1,(a6)+
        and.w   #0xFF,d0
        add.w   d0,d0
        lea     tile_attr_table,a1
        move.w  (a1,d0.w),(a6)+
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
        bcs.s   vs_hi_dirty
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
* sprites_update: arcade sprite list (ARC_SPR) -> the field arrays of the two pools (SPR_F0/SPR_F1),
* written to VRAM by vram_flush at the next VBlank.  Pool 0 (SPR_OBJ..) gets the codes < 0x3700, pool 1
* (SPR_PRI1.., above the HI columns) the codes >= 0x3700 (HUD, texts: MAME's priority rule).  One pass
* per pool over the list keeps the arcade's order inside each pool.
* ------------------------------------------------------------------------------------------
sprites_update:
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
        lea     SPR_F0,a1
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
        /* pool 1 */
        lea     SPR_F1,a1
        lea     SPR_F1_SZ(a1),a2
        lea     SPR_F1_SZ(a2),a3
        lea     SPR_F1_SZ(a3),a4
        move.w  #SPR_PRI1_MAX,d7
        lea     ARC_SPR+6,a0
        move.w  (sp),d6
        moveq   #-1,d0                  /* select: code >= 0x3700 */
        bsr     su_pass
        move.w  #SPR_PRI1_MAX,d0
        sub.w   d7,d0
        move.w  d0,rt_spr_n1
        tst.w   d6
        bmi.s   9f
        /* pool 1 full (big title logos): the rest goes to pool 0, after its own sprites */
        move.w  rt_spr_n0,d1
        move.w  #SPR_OBJ_MAX,d7
        sub.w   d1,d7
        beq.s   9f
        add.w   d1,d1
        lea     SPR_F0,a1
        adda.w  d1,a1
        lea     SPR_F0_SZ(a1),a2
        lea     SPR_F0_SZ(a2),a3
        lea     SPR_F0_SZ(a3),a4
        move.w  d7,-(sp)
        moveq   #-1,d0
        bsr     su_pass
        move.w  (sp)+,d0
        sub.w   d7,d0
        add.w   d0,rt_spr_n0
9:      addq.l  #2,sp
        movea.l (sp)+,a6
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
        move.w  (a6,d5.w),d5            /* SCB4, bit 0 set = off screen */
        btst    #0,d5
        bne.s   8f
        and.w   #0x7C00,d2              /* colour << 10 */
        cmpi.w  #0x4000,d2
        bcc.s   1f
        lsr.w   #2,d2                   /* colour << 8 */
        add.w   #PAL_SPRITES<<8,d2
        bra.s   2f
1:      move.w  #PAL_SHADOW<<8,d2       /* shadow: dithered tile set (gfx2crom --shadow) */
        or.w    #0x4000,d3
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

* spr_x_table: arcade sprite x (10 bits) -> SCB4 word, -1 when off screen
spr_x_table:
        .set    i,0
        .rept   1024
        .set    nx, i - 15 - CROP_X
        .if (nx >= -16) && (nx <= 319)
        .word   (nx & 0x1FF) << 7
        .else
        .word   -1
        .endif
        .set    i,i+1
        .endr

* spr_col_table: arcade sprite colour (5 bits) -> high word: palette << 8, low word: tile OR mask
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

* pen0_table: one byte per arcade tile, 1 if it has at least 64 pen-0 pixels (gfx2crom.py --pen0)
pen0_table:
        .incbin "pen0.bin"

* mod_ncol: i mod NCOLSPR for i < 128
mod_ncol:
        .set    i,0
        .rept   128
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
