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
        .equ    rt_sprites_used,RT_VARS+0x15A   /* .w Neo Geo sprites used by the previous frame's list */
        .equ    rt_gear,        RT_VARS+0x15C   /* .b gear toggle state */
        .equ    rt_sprites_used1,RT_VARS+0x15E  /* .w same for the high-priority pool (SPR_PRI1..) */
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

        .equ    ARC_PALRAW,     0x10A000        /* 4 KB: arcade palette RAM 200000-200FFF as written by the game (xBRG444) */
        .equ    ARC_PALRAW_END, 0x10B000
        .equ    ARC_PALLAST,    0x10B000        /* 2 KB: copy of entries 0-1023 last converted */
        .equ    ARC_IO,         0x10B800        /* 256 bytes: arcade I/O 700000-7000FF (inputs filled by us, latches ignored) */
        .equ    ARC_IO_END,     0x10B900
        .equ    COL_CACHE,      0x10B900        /* 2 x NCOLSPR x 128 bytes: tiles last uploaded per column sprite */
        .equ    COL_CACHE_END,  0x10CF00

        .equ    RT_CLEAR_END,   0x10F000        /* RAM cleared at start; 0x10F000-0x10F2FF = runtime stack */
        .equ    RT_STACK,       BIOS_RAM        /* runtime stack top */

* ---- arcade I/O shadow offsets (arcade address - 0x700000) ----
        .equ    IO_DSW,         ARC_IO+0x00     /* .w */
        .equ    IO_P1P2,        ARC_IO+0x02     /* .w */
        .equ    IO_WHEEL,       ARC_IO+0x04     /* .w */
        .equ    IO_SYSTEM,      ARC_IO+0x08     /* .w */
        .equ    IO_OKIBANK,     ARC_IO+0x0D     /* .b */
        .equ    IO_OKI,         ARC_IO+0x0F     /* .b */

* ---- column engine constants ----
        .equ    NCOLSPR,        22              /* column sprites per layer (320 px + 15 px offset -> 21 needed) */
        .equ    SPR_LAYER1,     1               /* first Neo Geo sprite of layer 1 (back) */
        .equ    SPR_LAYER0,     SPR_LAYER1+NCOLSPR
        .equ    SPR_OBJ,        SPR_LAYER0+NCOLSPR      /* first sprite for the arcade sprite list */
        .equ    SPR_PRI1_MAX,   96              /* arcade sprites with code >= 0x3700 (HUD, text): above the HI columns */
        .equ    SPR_PRI1,       381-SPR_PRI1_MAX
        .equ    SPR_HI,         SPR_PRI1-NCOLSPR        /* layer-0 columns again, category-1 cells only: above the sprites */
        .equ    SPR_OBJ_MAX,    SPR_HI-SPR_OBJ
        .equ    HI_BLANK,       4254            /* an all-transparent tile (fills the non-priority cells) */
        .equ    PAL_ARC_BASE,   16              /* arcade palette entry i (bank 0) -> Neo Geo palette 16 + i/16 */
        .equ    PAL_TILES,      PAL_ARC_BASE    /* arcade tile colours 0-31 -> palettes 16-47 */
        .equ    PAL_SPRITES,    PAL_ARC_BASE+32 /* arcade sprite colours (entries 0x200+) -> palettes 48-63 */
        .equ    PAL_SHADOW,     80              /* shadow sprites */
        .equ    PAL_FIX,        1               /* fix layer text (fix palettes must be 0..15) */
        .equ    CROP_X,         32              /* arcade frame x of Neo Geo x=0 (visarea 8 + 24) */
        .equ    CROP_Y,         20              /* arcade frame y of Neo Geo y=0 (visarea 16 + 4) */
