* ram.i - Neo Geo work RAM layout of the Thunder Hoop port (keep in sync with tools/mkrom.py)
*
* 100000-101FFF  arcade VRAM (two 32x32 tilemaps, decrypted: the runtime decrypts every write, src/crypt.s)
* 102000-102FFF  arcade "screen RAM" (the game keeps encrypted pointers / checks there)
* 103000-1037FF  palette shadow (last arcade value converted, per colour)
* 103800-103FFF  runtime variables
* 104000-10BFFF  arcade work RAM FF8000-FFFFFF (stack top 10C000 = arcade 1000000)
* 10C000-10C7FF  arcade palette RAM 200000-2007FF (xBGR555)
* 10C800-10D7FF  arcade sprite RAM 440000-440FFF
* 10D800-10D85F  arcade I/O 700000-70005F (inputs written by the runtime, outputs read by it)
* 10D860-10D86F  arcade video registers 108000-10800F (scroll)
* 10D870-10F2FF  runtime buffers

        .equ    ARC_VRAM,       0x100000
        .equ    ARC_SCREEN,     0x102000
        .equ    PAL_SHADOW,     0x103000
        .equ    RT_VARS,        0x103800
        .equ    ARC_WRAM,       0x104000
        .equ    ARC_STACK,      0x10C000
        .equ    ARC_PAL,        0x10C000
        .equ    ARC_SPR,        0x10C800
        .equ    ARC_IO,         0x10D800
        .equ    ARC_VREGS,      0x10D860
        .equ    RT_BUF,         0x10D870

* arcade I/O (words, active low): +0 DSW2, +2 DSW1, +4 P1, +6 P2; +0xD OKI bank, +0xF OKI
        .equ    IO_DSW2,        ARC_IO+0
        .equ    IO_DSW1,        ARC_IO+2
        .equ    IO_P1,          ARC_IO+4
        .equ    IO_P2,          ARC_IO+6

* runtime variables
        .equ    rt_frame,       RT_VARS+0x00    /* .l VBlanks */
        .equ    rt_dirty0,      RT_VARS+0x04    /* .l tilemap 0 columns written since the last upload */
        .equ    rt_dirty1,      RT_VARS+0x08    /* .l tilemap 1 */
        .equ    rt_slotcol,     RT_VARS+0x10    /* 2 x 21 bytes: tilemap column held by each column sprite */
        .equ    rt_vblank,      RT_VARS+0x40    /* .b set by the VBlank handler */
        .equ    rt_started,     RT_VARS+0x41    /* .b the arcade program runs */
        .equ    rt_fid,         RT_VARS+0x42    /* .b fix frame id (1..255) */
        .equ    rt_sprdirty,    RT_VARS+0x43    /* .b the arcade updated its sprite list */
        .equ    rt_sprprev,     RT_VARS+0x44    /* .w entries scanned last time */
        .equ    rt_prof,        RT_VARS+0x48    /* 5 .w raster lines (profiling) */
        .equ    rt_err_pc,      RT_VARS+0x80    /* .l trap_error */
        .equ    rt_err_sr,      RT_VARS+0x84    /* .w */
        .equ    rt_err_regs,    RT_VARS+0x88    /* 16 .l */
        .equ    RT_CLEAR_END,   RT_VARS+0x100
        .equ    backup_area,    RT_VARS+0x700   /* 256 bytes the BIOS saves to backup RAM (MVS) */

* runtime buffers
        .equ    ENT_SH,         RT_BUF          /* 511 x 3 words: w0, w2, w3 of each arcade sprite entry last frame */
        .equ    ENT_FIX,        ENT_SH+511*6    /* 511 words: fix VRAM address the entry's text is in, 0 = none */
        .equ    FIX_CLRQ,       ENT_FIX+511*2   /* fix addresses to blank at the end of the frame (511 words) */
        .equ    FIX_STAMP,      RT_VARS+0x100   /* 1280 bytes: frame id of the last text written (by fix offset) */
        .equ    FIX_BLANK,      0x00FF

* Neo Geo sprite allocation (= priority, higher on top)
        .equ    SPR_TM1,        1               /* 21 column sprites: tilemap 1 (back) */
        .equ    SPR_TM0,        22              /* 21 column sprites: tilemap 0 */
        .equ    SPR_ARC,        43              /* arcade sprites, up to 380 */
        .equ    SPR_MAX,        381
        .equ    NCOL,           21

* Neo Geo palettes: 16..79 = arcade palettes 0..63; 0..15 = copies of arcade 48..63 for the text on the
* fix layer (the font uses colours 0x30-0x3F); PAL_FIX = the runtime's own error screen
        .equ    PAL_FIX,        0
        .equ    PAL_ARC,        16
        .equ    PAL_TEXT,       48

* screen: the arcade shows 320x240 (rows 16-255); the Neo Geo 320x224: arcade rows 32-255 (the intro's
* last line of text is at 240-255; the game's top HUD at 16-31 needs another layout: to do)
        .equ    CROP_Y,         32

* P2 ROM (bank 0, 0x200000): decryption tables (tools/mkcrypt.py)
        .equ    CT_T0,          0x200000
        .equ    CT_TP,          0x220000
        .equ    CT_BS,          0x240000
        .equ    CT_KR,          0x241000
        .equ    CT_BF,          0x241200
        .equ    CT_PAL,         0x241600
        .equ    CT_MAP8,        0x251600        /* 8x8 sprite code -> Neo Geo tile - 0x8000 (tools/gfx2crom.py) */

* arcade program
        .equ    ARC_IRQ6,       0x010CFA        /* arcade VBlank handler (vector 30) */
        .equ    ARC_RESUME,     0x000410        /* after the reset prologue patched by tools/mkrom.py */
