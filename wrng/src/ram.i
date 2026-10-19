* ram.i - work RAM layout of the port (0x100000-0x10F2FF; 0x10F300+ belongs to the BIOS)
*
* The arcade program addresses its VRAM at 0x100000-0x103FFF and its scroll registers at 0x108000:
* those addresses are plain Neo Geo RAM, so they are kept as-is (shadow tilemaps + vregs).  The other
* hardware regions of the arcade (work RAM FEC000, palette 200000, sprites 440000, I/O 700000) are
* relocated here by the build-time patcher (tools/mkrom.py) - keep the constants in sync with it.

        .equ    ARC_VRAM,       0x100000        /* 16 KB: layer 0 at +0, layer 1 at +0x2000 (plain, decrypted) */
        .equ    ARC_VRAM_END,   0x104000
        .equ    ARC_WRAM,       0x104000        /* 16 KB: arcade FEC000-FEFFFF (shared RAM incl. DS5002FP variables) */
        .equ    ARC_WRAM_END,   0x108000
        .equ    ARC_VREGS,      0x108000        /* 16 bytes: arcade 108000-10800F */
        .equ    ARC_PAL,        0x108400        /* 2 KB: arcade palette entries 0-1023 converted to Neo Geo format */
        .equ    ARC_PAL_END,    0x108C00
        .equ    ARC_SPR,        0x108C00        /* 4 KB: arcade 440000-440FFF */
        .equ    ARC_SPR_END,    0x109C00

* ---- runtime variables (0x109C00+) ----
        .equ    RT_VARS,        0x109C00
        .equ    rt_frame,       RT_VARS+0x00    /* .l frames since start */
        .equ    rt_vbl_flag,    RT_VARS+0x04    /* .b set by the VBlank handler */
        .equ    rt_start_req,   RT_VARS+0x05    /* .b PLAYER_START seen */
        .equ    rt_pal_dirty,   RT_VARS+0x08    /* 64 bytes: one flag per 16-colour palette (0..63) */
        .equ    rt_pal_any,     RT_VARS+0x48    /* .b any palette dirty */
        .equ    rt_pal_ahead,   RT_VARS+0x49    /* .b the running tick converted its palettes (ARC_PAL ahead of the screen) */
        .equ    rt_col_dirty,   RT_VARS+0x50    /* 2 x 64 bytes: dirty flag per (layer, map column) */
        .equ    rt_col_loaded,  RT_VARS+0xD0    /* 2 x 24 bytes: map column currently loaded in column sprite s (0xFF none) */
        .equ    rt_err_regs,    RT_VARS+0x100   /* 16 longs */
        .equ    rt_err_pc,      RT_VARS+0x140
        .equ    rt_err_sr,      RT_VARS+0x144
        .equ    rt_input,       RT_VARS+0x150   /* .w arcade-format P1_P2 word built from Neo Geo inputs */
        .equ    rt_input_sys,   RT_VARS+0x152   /* .w arcade SYSTEM word */
        .equ    rt_dsw,         RT_VARS+0x154   /* .w arcade DSW word */
        .equ    rt_coin_pulse,  RT_VARS+0x156   /* .b frames left of a synthetic coin press */
        .equ    rt_start_pulse, RT_VARS+0x157   /* .b frames left of a synthetic start press */
        .equ    rt_okibank,     RT_VARS+0x158   /* .b last OKI bank written */
        .equ    rt_okiphrase,   RT_VARS+0x159   /* .b last OKI phrase byte written (0x80|phrase) */
        .equ    rt_sprites_used,RT_VARS+0x15A   /* .w end of pool 0 as last written to VRAM (vram_flush) */
        .equ    rt_gear,        RT_VARS+0x15C   /* .b gear toggle state */
        .equ    rt_sprites_used1,RT_VARS+0x15E  /* .w same for pool 1 (SPR_PRI1..) */
        .equ    rt_saved_sp,    RT_VARS+0x160   /* .l SP of the arcade stack while its IRQ handler runs on ours */
        .equ    rt_prev_b,      RT_VARS+0x164   /* .b previous B button (gear toggle edge) */
        .equ    rt_in_game,     RT_VARS+0x165   /* .b */
        .equ    rt_io_sp,       RT_VARS+0x170   /* .l SP when SYSTEM_IO was called (resume point after a BIOS re-entry) */
        .equ    rt_arcade_on,   RT_VARS+0x174   /* .b arcade program running */
        .equ    rt_bios_cred,   RT_VARS+0x178   /* .b last BIOS credit byte seen (MVS backup RAM 0xD00034, BCD) */
        .equ    rt_coins_due,   RT_VARS+0x179   /* .b coins still to inject into the arcade */
        .equ    rt_coin_seq,    RT_VARS+0x17A   /* .b coin pulse state (0 idle) */
        .equ    rt_start_wait,  RT_VARS+0x17B   /* .b frames to wait before holding START */
        .equ    rt_start_hold,  RT_VARS+0x17C   /* .w frames left holding START (until the arcade consumes a credit) */
        .equ    rt_cred_start,  RT_VARS+0x17E   /* .w arcade credits when the START hold began */
        .equ    rt_c0,          RT_VARS+0x168   /* 2 words: unwrapped first map column of the window (layer 0, layer 1) */
        .equ    rt_trap_stub,   RT_VARS+0x16C   /* .l */
        .equ    rt_test_out,    RT_VARS+0x180   /* 32 longs: self-test results (decrypt check) */

        .equ    backup_area,    RT_VARS+0x200   /* 256 bytes saved to MVS backup RAM (unused for now) */
        .equ    rt_hi_cnt,      RT_VARS+0x300   /* NCOLSPR bytes: category-1 cells in the layer-0 column of sprite s */
        .equ    rt_bg_cnt,      RT_VARS+0x318   /* NCOLSPR bytes: layer-1 cells needing an opaque backdrop, per column sprite s */
        .equ    rt_bg_recount,  RT_VARS+0x330   /* .b a tile palette changed: recount rt_bg_cnt next frame */
        .equ    rt_eff_cp,      RT_VARS+0x334   /* .l sl_eff: EFF_DESC entry of the current headlight column */
        .equ    rt_eff_tp,      RT_VARS+0x338   /* .l sl_eff: next free tile slot */
        .equ    rt_cl_budget,   RT_VARS+0x33C   /* .b column loads left this frame (layer_update pass 1) */
        .equ    rt_cl_more,     RT_VARS+0x33D   /* .b some were left for the next frame */
        .equ    CL_BUDGET,      8               /* about 2.3 lines of VBlank each: a whole new screen loads in about
                                                   6 frames without writing much VRAM into the visible lines */
        .equ    rt_scb2_rot,    RT_VARS+0x363   /* .b video_vblank: next group of 24 sprites whose SCB2 shrink is rewritten */
        .equ    rt_backdrop,    RT_VARS+0x332   /* .w Neo Geo backdrop colour for the next VBlank (colour 0 of the central layer-1 cell) */
        .equ    rt_dbg,         RT_VARS+0x340   /* 8 words: raster counter (REG_LSPCMODE) at frame_hook milestones (profiling) */
* presentation queue: frame_hook seals each frame it prepares (end of its VRAM commands in the ring, the
* sprite set it filled); every VBlank shows the oldest sealed frame.  The main loop runs up to one tick
* ahead, so a tick that lasts more than a frame does not leave a VBlank without a new frame.
        .equ    rt_vcmd_rd,     RT_VARS+0x350   /* .l next command to play in the ring */
        .equ    rt_pend_n,      RT_VARS+0x354   /* .b sealed frames not shown yet (0..2) */
        .equ    rt_fill_set,    RT_VARS+0x355   /* .b sprite set (0/1) the frame in preparation fills */
        .equ    rt_noseal,      RT_VARS+0x356   /* .b VBlanks with no sealed frame (2: no frame work runs, play the queue as is) */
        .equ    rt_pend_q,      RT_VARS+0x358   /* 2 x .l end of the commands of sealed frame 0 (oldest), 1 */
        .equ    rt_pend_s,      RT_VARS+0x360   /* 2 x .b sprite set of sealed frame 0, 1 */
        .equ    rt_set_cnt,     RT_VARS+0x368   /* 2 x (.w pool 0 sprites, .w pool 1 sprites, .w headlight columns, .w -) per set */
        .equ    rt_eff_base,    RT_VARS+0x378   /* .l EFF_DESC of the set being filled (its tiles at +EFF_TILES_OFS) */
        .equ    rt_spr_conv,    RT_VARS+0x357   /* .b spr_lists converted the sprite lists in this frame */
        .equ    rt_set_base,    RT_VARS+0x37C   /* .l SPR_F0 of the set being filled (pool 1 at +SPR_F1_OFS) */
        .equ    rt_pal0_black,  RT_VARS+0x3C0   /* 32 bytes: 1 if colour 0 of tile palette p is black (backdrop recount trigger) */
        .equ    rt_vcmd_ptr,    RT_VARS+0x3E0   /* .l end of the commands queued in VCMD_BUF */
        .equ    rt_spr_n0,      RT_VARS+0x3E4   /* .w sprites prepared in pool 0 (for the next flush) */
        .equ    rt_spr_n1,      RT_VARS+0x3E6   /* .w sprites prepared in pool 1 */
        .equ    rt_lu_sig,      RT_VARS+0x3E8   /* 2 x (.w SCB4 x0, .w SCB3 y) of the positions queued last (layer 0, layer 1) */
        .equ    rt_lu_force,    RT_VARS+0x3F0   /* 2 x .b positions must be queued again (column reloaded / companion shown or hidden) */
        .equ    rt_irq_line,    RT_VARS+0x3F4   /* .w REG_LSPCMODE at the VBlank interrupt entry (profiling) */
        .equ    rt_in_hook,     RT_VARS+0x3F6   /* .b the VBlank handler is running the frame work (interruptible) */
        .equ    rt_pal_half,    RT_VARS+0x3F7   /* .b palette_sync: quarter checked last time (0..3) */
        .equ    rt_spr_busy,    RT_VARS+0x3F8   /* .b sprites_update is filling the field arrays (a nested VBlank flush leaves the sprites as they are) */
        .equ    rt_oki_cnt,     RT_VARS+0x380   /* 4 words: frames left of the phrase on OKI channel n (emulated busy bits) */
        .equ    rt_diag_hold,   RT_VARS+0x388   /* .b frames D has been held (diagnostic overlay toggle) */
        .equ    rt_diag_on,     RT_VARS+0x389   /* .b diagnostic overlay shown */
        .equ    rt_diag_clr,    RT_VARS+0x38A   /* .b overlay just turned off: blank it once */
        .equ    rt_skip,        RT_VARS+0x38C   /* .w arcade ticks skipped (the frame work was still running at VBlank) */
        .equ    rt_mwait,       RT_VARS+0x390   /* .b the arcade main loop is in one of its "wait for the tick" loops (main_wait) */
        .equ    rt_mw_age,      RT_VARS+0x391   /* .b VBlanks since the main loop last entered main_wait (saturates at 255) */
        .equ    rt_tick_pend,   RT_VARS+0x392   /* .b ticks owed to the main loop (0..2): one per VBlank, main_wait runs one at the end of each frame */
        .equ    rt_col_inval,   RT_VARS+0x393   /* .b rt_col_loaded entries were invalidated (layer_update must check every column) */
        .equ    rt_idle,        RT_VARS+0x3A0   /* .l main_wait loop iterations (profiling: idle time of the main loop) */
        .equ    rt_st_cnt,      RT_VARS+0x3A4   /* .b VBlanks left before the next per-second statistics */
        .equ    rt_st_ticks,    RT_VARS+0x3A8   /* .l rt_ticks one second ago */
        .equ    rt_st_idle,     RT_VARS+0x3AC   /* .l rt_idle one second ago */
        .equ    rt_tps,         RT_VARS+0x3B0   /* .w arcade ticks in the last second, BCD (60 = full speed; diagnostic overlay) */
        .equ    rt_idlepct,     RT_VARS+0x3B2   /* .w idle time of the main loop in the last second, %, BCD */
        .equ    rt_snd_owed,    RT_VARS+0x39D
        .equ    rt_eff_n,       RT_VARS+0x3B6   /* .w headlight columns prepared (spr_lists) */
        .equ    rt_eff_nt,      RT_VARS+0x3B8   /* .w their tiles */
        .equ    rt_eff_last,    RT_VARS+0x3BA   /* .w SCB4, .w attribute, .w SCB3 of the last tile added (3 words, to 0x3BF) */
        .equ    rt_shown,       RT_VARS+0x3FC   /* .l sealed frames shown (profiling: VBlanks - this = frames repeated) */
        .equ    rt_eff_used,    RT_VARS+0x3FA   /* .w columns written to VRAM last time */
        .equ    rt_cmp_any,     RT_VARS+0x3B4   /* 2 x .b some HI / BG companion column is marked dirty (layer_update recounts) */
        .equ    rt_prev_sel,    RT_VARS+0x39E
        .equ    rt_pal_rot,     RT_VARS+0x39F   /* .b video_vblank: next group of 4 palettes to rewrite (0..15) */   /* .b SELECT pressed last frame (coin button, edge) */   /* .b sound sequencer runs owed: a VBlank came in the middle of a tick */
        .equ    rt_spr_slow,    RT_VARS+0x39C   /* .b spr_lists left the lists to the multi-pass conversion (sprites_slow, in video_frame) */
        .equ    rt_ticks,       RT_VARS+0x398   /* .l arcade ticks run (frame_hook calls; profiling) */
        .equ    rt_lu_c0last,   RT_VARS+0x394   /* 2 x .w first map column checked by the last pass 1 (layer 0, layer 1) */

        .equ    ARC_PALRAW,     0x10A000        /* 4 KB: arcade palette RAM 200000-200FFF as written by the game (xBRG444) */
        .equ    ARC_PALRAW_END, 0x10B000
        .equ    ARC_PALLAST,    0x10B000        /* 2 KB: copy of entries 0-1023 last converted */
        .equ    ARC_IO,         0x10B800        /* 256 bytes: arcade I/O 700000-7000FF (inputs filled by us, latches ignored) */
        .equ    ARC_IO_END,     0x10B900
* sprite fields prepared by sprites_update and written to VRAM by vram_flush at the next VBlank:
* per pool four word arrays (SCB1 tile, SCB1 attribute, SCB3, SCB4), one entry per Neo Geo sprite
        .equ    SPR_F0,         0x10B900        /* pool 0 (SPR_OBJ..): 4 x SPR_OBJ_MAX words */
        .equ    SPR_F0_SZ,      SPR_OBJ_MAX*2
        .equ    SPR_F1,         SPR_F0+4*SPR_F0_SZ      /* pool 1 (SPR_PRI1..): 4 x SPR_PRI1_MAX words */
        .equ    SPR_F1_SZ,      SPR_PRI1_MAX*2
        .equ    SPR_F_END,      SPR_F1+4*SPR_F1_SZ
        .equ    SPR_F1_OFS,     4*SPR_OBJ_MAX*2 /* SPR_F1 - SPR_F0 (written out: a difference of forward symbols assembles as 0) */
        .equ    SPR_SET_SZ,     0x780           /* >= SPR_F_END-SPR_F0; set 1 right after set 0 */
        .equ    SPR_SET1,       SPR_F0+SPR_SET_SZ
* VRAM command buffer: blocks of (address.w, VRAMMOD.w, count-1.w, count data words), written by the
* frame preparation and by vram_store, replayed by vram_flush at the start of the next VBlank so that the
* screen never changes while it is being drawn
        .equ    VCMD_BUF,       SPR_SET1+SPR_SET_SZ     /* a ring: a block that would not fit before VCMD_END */
        .equ    VCMD_END,       CELL_ROWS               /* leaves the word 0xFFFF and starts again at VCMD_BUF */
* map cells changed in the shown columns (vram_store): one bit per column sprite s, per map row and layer;
* video_frame (cells_flush) writes each row as runs of consecutive sprites (VRAMMOD 64: one block per
* run instead of one per cell)
        .equ    CELL_ROWS,      0x10E5F8        /* 2 x .l rows with marked cells (layer 0, layer 1) */
        .equ    CELL_MASK,      0x10E600        /* 2 x 32 x .l sprites marked per row */
        .equ    CELL_END,       0x10E700
        .equ    ARC_PALB7,      0x10E700        /* 2 KB: arcade palette bank 7 (203800-203FFF: the night stages' undimmed colours) */
        .equ    ARC_PALB7_END,  0x10EF00
        .equ    VCMD_ROOM,      256             /* bytes reserved after a vc_room call (one or a few blocks: up to 162 bytes) */
* companion columns (HI over layer 0, BG under layer 1): only the rows r0 .. r0+h-1 of the map column that
* hold something are uploaded and displayed (sprites per scanline); a cell changing category marks the
* column dirty and it is recounted on the next frame
        .equ    CMP_HI_R0,      0x10EF00        /* NCOLSPR bytes: first map row shown by HI sprite SPR_HI+s */
        .equ    CMP_HI_H,       0x10EF18        /* NCOLSPR bytes: its height in tiles */
        .equ    CMP_BG_R0,      0x10EF30
        .equ    CMP_BG_H,       0x10EF48
        .equ    CMP_HI_DIRTY,   0x10EF60        /* NCOLSPR bytes */
        .equ    CMP_BG_DIRTY,   0x10EF78
        .equ    LAY_H,          0x10EF90        /* 2 x 24 bytes: tiles shown by layer column s (layer 0, then layer 1): past its last non-empty row, 0 = empty */

        .equ    EYE_SHOWN,      0x10EFFC        /* .l EYE_MAGIC once the start-up screen was shown (not cleared) */
        .equ    EYE_MAGIC,      0x47414531      /* "GAE1" */
        .equ    RT_CLEAR_END,   0x10EFFC        /* RAM cleared at start; 0x10F000-0x10F2FF = runtime stack */
        .equ    RT_STACK,       BIOS_RAM        /* runtime stack top */

* ---- arcade I/O shadow offsets (arcade address - 0x700000) ----
        .equ    IO_DSW,         ARC_IO+0x00     /* .w */
        .equ    IO_P1P2,        ARC_IO+0x02     /* .w */
        .equ    IO_WHEEL,       ARC_IO+0x04     /* .w */
        .equ    IO_SYSTEM,      ARC_IO+0x08     /* .w */
        .equ    IO_OKIBANK,     ARC_IO+0x0D     /* .b */
        .equ    IO_OKI,         ARC_IO+0x0F     /* .b */

* ---- column engine constants ----
        .equ    NCOLSPR,        24              /* column sprites per layer: 14-pixel (shrunk) columns, 321 px + 13 px fine scroll -> 24 */
        .equ    SPR_BG,         1               /* backdrop columns: palette colour 0 behind the layer-1 cells (the arcade's back layer is opaque) */
        .equ    SPR_LAYER1,     SPR_BG+NCOLSPR  /* first Neo Geo sprite of layer 1 (back) */
        .equ    SPR_LAYER0,     SPR_LAYER1+NCOLSPR
        .equ    SOLID_TILE,     1594            /* a tile with every pixel = pen 15 (backdrop cells) */
        .equ    PAL_BG,         112             /* 32 palettes: colour 15 = colour 0 of tile palette 0..31 */
        .equ    SPR_OBJ,        SPR_LAYER0+NCOLSPR      /* first sprite for the arcade sprite list */
        .equ    SPR_PRI1_MAX,   96              /* arcade sprites with code >= 0x3700 (HUD, text): above the HI columns */
        .equ    SPR_PRI1,       381-SPR_PRI1_MAX
        .equ    SPR_HI,         SPR_PRI1-NCOLSPR        /* layer-0 columns again, category-1 cells only: above the sprites */
        .equ    EFF_MAX,        48              /* night stages: columns of headlight tiles (one sprite each, up to 16 tiles) */
        .equ    SPR_EFF,        SPR_HI-EFF_MAX
        .equ    SPR_OBJ_MAX,    SPR_EFF-SPR_OBJ
        .equ    EFF_DESC,       ARC_SPR         /* EFF_MAX x (.w SCB3 of the top tile | height, .w SCB4, .w first tile, .w tiles) */
        .equ    EFF_TILES,      ARC_SPR+0x200   /* EFF_TILES_MAX x (.w tile, .w attribute); ARC_SPR is only used by the */
        .equ    EFF_TILES_OFS,  0x200
        .equ    EFF_SET1,       ARC_SPR+0x600   /* the second set of headlight columns (set 0 at ARC_SPR) */
        .equ    EFF_TILES_MAX,  256             /* fallback copy of 0x44E6, which then leaves no columns (sl_slow) */
        .equ    HI_BLANK,       4254            /* an all-transparent tile (fills the non-priority cells) */
        .equ    PAL_ARC_BASE,   16              /* arcade palette entry i (bank 0) -> Neo Geo palette 16 + i/16 */
        .equ    PAL_TILES,      PAL_ARC_BASE    /* arcade tile colours 0-31 -> palettes 16-47 */
        .equ    PAL_SPRITES,    PAL_ARC_BASE+32 /* arcade sprite colours (entries 0x200+) -> palettes 48-63 */
        .equ    PAL_SHADOW,     80              /* shadow sprites */
        .equ    PAL_LIGHT,      81              /* the same sprites in the night stages: headlight beams (gfx2crom --light) */
        .equ    PAL_FIX,        1               /* fix layer text (fix palettes must be 0..15) */
        .equ    CROP_X,         9               /* arcade frame x of Neo Geo x=0 (visarea 8 + 1) */
        .equ    SCB2_SHRINK,    0x0DFF          /* every sprite 14 pixels wide: the 368-pixel arcade screen x 7/8 = 322 -> the whole width in 320 */
        .equ    CROP_Y,         20              /* arcade frame y of Neo Geo y=0 (visarea 16 + 4) */
