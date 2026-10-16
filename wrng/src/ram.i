* ram.i - work RAM layout of the port (0x100000-0x10F2FF; 0x10F300+ belongs to the BIOS)
*
* The arcade program addresses its VRAM at 0x100000-0x103FFF and its scroll registers at 0x108000:
* those addresses are plain Neo Geo RAM, so they are kept as-is (shadow tilemaps + vregs) and only the
* remaining hardware regions of the arcade (work RAM FEC000, palette 200000, sprites 440000, I/O 700000)
* are relocated by the build-time patcher.

        .equ    ARC_VRAM,       0x100000        /* 16 KB: layer 0 at +0, layer 1 at +0x2000 (plain, not encrypted) */
        .equ    ARC_VRAM_END,   0x104000
        .equ    ARC_WRAM,       0x104000        /* 16 KB: arcade FEC000-FEFFFF */
        .equ    ARC_WRAM_END,   0x108000
        .equ    ARC_VREGS,      0x108000        /* 16 bytes: arcade 108000-10800F */
        .equ    ARC_PAL,        0x108400        /* 2 KB: arcade palette bank 0 (entries 0-1023), NEO GEO format */
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
        .equ    rt_sprites_used,RT_VARS+0x15A   /* .w Neo Geo sprites used by the previous frame's list */

        .equ    backup_area,    RT_VARS+0x200   /* 256 bytes saved to MVS backup RAM (unused for now) */

        .equ    RT_CLEAR_END,   0x10F000        /* RAM cleared at start; 0x10F000-0x10F2FF = stack */
        .equ    RT_STACK,       BIOS_RAM        /* runtime stack top */

* ---- column engine constants ----
        .equ    NCOLSPR,        22              /* column sprites per layer (320 px + 15 px offset -> 21 needed) */
        .equ    SPR_LAYER1,     1               /* first Neo Geo sprite of layer 1 (back) */
        .equ    SPR_LAYER0,     SPR_LAYER1+NCOLSPR
        .equ    SPR_OBJ,        SPR_LAYER0+NCOLSPR      /* first sprite for the arcade sprite list */
        .equ    SPR_OBJ_MAX,    380-SPR_OBJ
        .equ    PAL_ARC_BASE,   16              /* arcade palette entry i (bank 0) -> Neo Geo palette 16 + i/16 */
        .equ    PAL_TILES,      PAL_ARC_BASE    /* arcade tile colours 0-31 -> palettes 16-47 */
        .equ    PAL_SPRITES,    PAL_ARC_BASE+32 /* arcade sprite colours (entries 0x200+) -> palettes 48-63 */
        .equ    PAL_SHADOW,     80              /* shadow sprites */
        .equ    PAL_FIX,        1               /* fix layer text (fix palettes must be 0..15) */
        .equ    CROP_X,         32              /* arcade frame x of Neo Geo x=0 (visarea 8 + 24) */
        .equ    CROP_Y,         20              /* arcade frame y of Neo Geo y=0 (visarea 16 + 4) */
