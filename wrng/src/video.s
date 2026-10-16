* video.s - Neo Geo video layer of the port.
*
* The arcade has two 64x32 tilemaps of 16x16 tiles (1024x512 px, wrap-around scrolling) plus 16x16 sprites.
* Neo Geo has no tilemaps, so each layer is drawn with NCOLSPR column sprites, each 32 tiles (512 px) tall =
* a whole map column; vertical scrolling is just the sprite Y (the 9-bit sprite space wraps at 512 lines),
* horizontal scrolling moves the columns and re-loads one column per 16 px scrolled. The tiles were
* converted 1:1 (gfx2crom.py), so an arcade tile code is a Neo Geo tile number.
*
* Sprite order (= priority): layer 1 (opaque, back) < layer 0 < arcade sprite list.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global video_init, video_init_min, video_vblank, video_frame
        .global pal_write, vram_mark_dirty, video_mark_all_dirty, pal_table, vram_store

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
        move.w  #VRAM_SCB3,REG_VRAMADDR
        move.w  #511,d0
2:      move.w  #0,REG_VRAMRW
        dbra    d0,2b
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
        /* shadow palette: pens 1..7 unused (black), 8..15 darker and darker (first version: solid dark) */
        lea     PALETTES+PAL_SHADOW*32,a0
        move.w  #0x8000,(a0)+
        moveq   #14,d0
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
        tst.b   rt_pal_any
        beq.s   9f
        clr.b   rt_pal_any
        lea     rt_pal_dirty,a0
        lea     ARC_PAL,a1
        lea     PALETTES+PAL_ARC_BASE*32,a2
        moveq   #63,d0
1:      tst.b   (a0)+
        beq.s   2f
        clr.b   -1(a0)
        movem.l (a1),d1-d7/a3           /* 16 words = 8 longs */
        movem.l d1-d7/a3,(a2)
2:      lea     32(a1),a1
        lea     32(a2),a2
        dbra    d0,1b
9:      rts

* ------------------------------------------------------------------------------------------
* video_frame: called once per frame by the game loop after the arcade logic ran
video_frame:
        /* layer 1 (back) first */
        move.w  ARC_VREGS+6,d0          /* scrollx layer 1 */
        move.w  ARC_VREGS+4,d1          /* scrolly layer 1 */
        lea     ARC_VRAM+0x2000,a3
        lea     rt_c0+2,a4
        lea     rt_col_loaded+24,a5
        move.w  #SPR_LAYER1,d7
        bsr     layer_update
        move.w  ARC_VREGS+2,d0          /* scrollx layer 0 (+4 as in MAME) */
        addq.w  #4,d0
        move.w  ARC_VREGS+0,d1
        lea     ARC_VRAM,a3
        lea     rt_c0,a4
        lea     rt_col_loaded,a5
        move.w  #SPR_LAYER0,d7
        bsr     layer_update
        bra     sprites_update

* layer_update: d0 = scrollx, d1 = scrolly, a3 = map base (shadow VRAM), a4 = rt_c0 slot of the layer,
*               a5 = loaded-column table, d7 = first Neo Geo sprite of the layer.
* Each column sprite s shows map column c = loaded[s]; its SCB1 tiles are re-uploaded fully when the
* column changes, otherwise only the tiles whose pair differs from the cached copy.
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
        neg.w   d2                      /* x of window column 0 */
        move.w  d0,d3
        lsr.w   #4,d3                   /* c0 = first map column (unwrapped, 0..66) */
        move.w  d3,(a4)                 /* rt_c0 of this layer, for vram_store */
        move.w  d3,d4
        ext.l   d4
        divu    #NCOLSPR,d4
        swap    d4                      /* d4.w = c0 mod NCOLSPR = sprite of window column 0 */
        lea     tile_attr_table,a1
        moveq   #0,d5                   /* j = window column */
lu_loop:
        move.w  d4,d0
        add.w   d5,d0
        cmpi.w  #NCOLSPR,d0
        bcs     1f
        sub.w   #NCOLSPR,d0             /* d0 = s */
1:      move.w  d3,d1
        add.w   d5,d1
        and.w   #63,d1                  /* d1 = map column */
        cmp.b   (a5,d0.w),d1
        beq     lu_pos                  /* already loaded (kept up to date by vram_store) */
        movem.l d0-d5,-(sp)
        move.b  d1,(a5,d0.w)            /* loaded[s] = column */
        move.w  d1,d2
        lsl.w   #2,d2
        lea     (a3,d2.w),a0            /* a0 = map column (row 0) */
        add.w   d7,d0
        lsl.w   #6,d0                   /* d0 = SCB1 address of the sprite */
        move.w  d0,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        moveq   #31,d2
4:      move.l  (a0),d4
        swap    d4
        and.w   #0x3FFF,d4
        move.w  d4,REG_VRAMRW
        moveq   #0,d4
        move.b  3(a0),d4
        add.w   d4,d4
        move.w  (a1,d4.w),REG_VRAMRW
        lea     256(a0),a0
        dbra    d2,4b
        movem.l (sp)+,d0-d5
lu_pos:
        /* position: SCB4 x, SCB3 y (d0 = s, d2 = x) */
        add.w   d7,d0
        move.w  d0,d1
        add.w   #VRAM_SCB4,d1
        move.w  d1,REG_VRAMADDR
        move.w  d2,d1
        and.w   #0x1FF,d1
        lsl.w   #7,d1
        move.w  d1,REG_VRAMRW
        add.w   #VRAM_SCB3,d0
        move.w  d0,REG_VRAMADDR
        move.w  d6,REG_VRAMRW
        add.w   #16,d2
        addq.w  #1,d5
        cmpi.w  #NCOLSPR,d5
        bcs     lu_loop
        rts

* ------------------------------------------------------------------------------------------
* vram_store: d0.l = tile pair (code, attr), a0 = shadow VRAM address (0x100000-0x103FFF).
* Stores the pair and, if the map column is shown by a column sprite, writes the tile to its SCB1.
* Trashes d0-d2/a0-a2.
* ------------------------------------------------------------------------------------------
vram_store:
        move.l  d0,(a0)
        move.l  a0,d1
        lea     rt_col_loaded,a1
        move.w  rt_c0,d2
        move.w  #SPR_LAYER0,a2          /* a2.w = sprite base of the layer */
        btst    #13,d1
        beq     1f
        lea     24(a1),a1
        move.w  rt_c0+2,d2
        move.w  #SPR_LAYER1,a2
1:      and.w   #0x1FFF,d1
        lsr.w   #2,d1                   /* tile index */
        move.w  d1,-(sp)                /* keep: col = low 6 bits, row = bits 6-10 */
        and.w   #63,d1                  /* col */
        move.w  d1,-(sp)
        sub.w   d2,d1
        and.w   #63,d1                  /* j = (col - c0) & 63 */
        cmpi.w  #NCOLSPR,d1
        bcc     9f                      /* not in the window */
        add.w   d1,d2                   /* c0 + j (< 128) */
        lea     mod_ncol,a0
        move.b  (a0,d2.w),d2
        and.w   #0xFF,d2                /* s */
        move.w  (sp),d1                 /* col */
        cmp.b   (a1,d2.w),d1
        bne     9f                      /* column not loaded yet (will be loaded whole) */
        move.w  a2,d1
        add.w   d2,d1
        lsl.w   #6,d1                   /* SCB1 base of sprite s */
        move.w  2(sp),d2
        lsr.w   #6,d2                   /* row */
        add.w   d2,d2
        add.w   d2,d1
        move.w  d1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        move.l  d0,d1
        swap    d1
        and.w   #0x3FFF,d1
        move.w  d1,REG_VRAMRW
        and.w   #0xFF,d0
        add.w   d0,d0
        lea     tile_attr_table,a1
        move.w  (a1,d0.w),REG_VRAMRW
9:      addq.l  #4,sp
        rts

* ------------------------------------------------------------------------------------------
* sprites_update: arcade sprite RAM (ARC_SPR) -> Neo Geo sprites SPR_OBJ..
* ------------------------------------------------------------------------------------------
sprites_update:
        lea     ARC_SPR+6,a0
        move.w  #SPR_OBJ,d7
        /* entries written this frame by the arcade copy routine 0x44E6: the three list counters */
        move.w  ARC_WRAM+0x27E,d6
        add.w   ARC_WRAM+0x280,d6
        add.w   ARC_WRAM+0x282,d6
        add.w   ARC_WRAM+0x284,d6
        cmpi.w  #(0x1000-6)/8,d6
        bls     1f
        move.w  #(0x1000-6)/8,d6
1:      subq.w  #1,d6
        bmi     su_done
su_loop:
        move.w  (a0)+,d0                /* w0: y, flips */
        move.w  (a0)+,d1                /* w1: unused */
        move.w  (a0)+,d2                /* w2: x, colour */
        move.w  (a0)+,d3                /* w3: code */
        /* y: sy = (240 - (w0 & 0xFF)) & 0xFF ; ny = sy - CROP_Y ; visible if -15 <= ny <= 223 */
        moveq   #0,d4
        move.b  d0,d4
        neg.w   d4
        add.w   #240,d4
        and.w   #0xFF,d4
        sub.w   #CROP_Y-15,d4           /* ny + 15 */
        cmpi.w  #223+15,d4
        bhi.w   su_next
        /* x: xpos = (w2 & 0x3FF) - 15 ; nx = xpos - CROP_X ; visible if -15 <= nx <= 319 */
        move.w  d2,d5
        and.w   #0x3FF,d5
        sub.w   #15+CROP_X-16,d5        /* nx + 16 */
        and.w   #0x3FF,d5
        cmpi.w  #319+16,d5
        bhi.w   su_next
        /* SCB1: tile, attr */
        move.w  d7,d1
        lsl.w   #6,d1
        move.w  d1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        and.w   #0x3FFF,d3
        move.w  d3,REG_VRAMRW
        move.w  d2,d1
        lsr.w   #8,d1
        and.w   #0x7C,d1                /* colour<<2 */
        lsr.w   #2,d1
        btst    #4,d1
        beq.s   1f
        moveq   #PAL_SHADOW,d1
        bra.s   2f
1:      add.w   #PAL_SPRITES,d1
2:      lsl.w   #8,d1
        rol.w   #2,d0                   /* bits 15,14 -> 1,0 (vflip, hflip) */
        and.w   #3,d0
        or.w    d0,d1
        move.w  d1,REG_VRAMRW
        /* SCB3: y */
        move.w  d7,d1
        add.w   #VRAM_SCB3,d1
        move.w  d1,REG_VRAMADDR
        sub.w   #15,d4                  /* ny */
        neg.w   d4
        add.w   #496,d4
        and.w   #0x1FF,d4
        lsl.w   #7,d4
        addq.w  #1,d4                   /* height 1 */
        move.w  d4,REG_VRAMRW
        /* SCB4: x */
        move.w  d7,d1
        add.w   #VRAM_SCB4,d1
        move.w  d1,REG_VRAMADDR
        sub.w   #16,d5
        and.w   #0x1FF,d5
        lsl.w   #7,d5
        move.w  d5,REG_VRAMRW
        addq.w  #1,d7
        cmpi.w  #SPR_OBJ+SPR_OBJ_MAX,d7
        bcc.s   su_done
su_next:
        dbra    d6,su_loop
su_done:
        /* hide the sprites used last frame but not now */
        move.w  rt_sprites_used,d0
        move.w  d7,rt_sprites_used
        sub.w   d7,d0
        ble.s   9f
        subq.w  #1,d0
        move.w  d7,d1
        add.w   #VRAM_SCB3,d1
        move.w  d1,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
1:      move.w  #0,REG_VRAMRW
        dbra    d0,1b
9:      rts

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
