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
        .equ    IO_OKI,         ARC_IO+0x0F     /* .b emulated OKI status: bit n = channel n busy */

* runtime variables
        .equ    rt_frame,       RT_VARS+0x00    /* .l VBlanks */
        .equ    rt_dirty0,      RT_VARS+0x04    /* .l tilemap 0 columns written since the last upload */
        .equ    rt_dirty1,      RT_VARS+0x08    /* .l tilemap 1 */
        .equ    rt_slotcol,     RT_VARS+0x10    /* 2 x 21 bytes: tilemap column held by each column sprite */
        .equ    rt_hion,        RT_VARS+0x6A    /* 21 bytes: the HI column sprite has priority-1 cells */
        .equ    rt_vblank,      RT_VARS+0x40    /* .b set by the VBlank handler */
        .equ    rt_started,     RT_VARS+0x41    /* .b the arcade program runs */
        .equ    rt_fid,         RT_VARS+0x42    /* .b fix frame id (1..255) */
        .equ    rt_sprdirty,    RT_VARS+0x43    /* .b the arcade updated its sprite list */
        .equ    rt_paldirty,    RT_VARS+0x47    /* .b the game loaded the palette base (rt_pal_an) */
        .equ    rt_sprprev,     RT_VARS+0x44    /* .w entries scanned last time */
        .equ    rt_mode,        RT_VARS+0x46    /* .b 1: game layout (something in the top HUD band) */
        .equ    rt_topcnt,      RT_VARS+0x48    /* .w sprite entries in the top HUD band (arcade rows 16-31) */
        .equ    rt_pcrop,       RT_VARS+0x4A    /* .w first arcade row of the playfield on Neo Geo line 0 */
        .equ    rt_okibank,     RT_VARS+0x50    /* .b last OKI bank written */
        .equ    rt_okiphrase,   RT_VARS+0x51    /* .b last OKI phrase byte (0x80 | phrase) */
        .equ    rt_oki_cnt,     RT_VARS+0x54    /* 4 .w frames left of the phrase on each OKI channel */
        .equ    rt_prof,        RT_VARS+0x60    /* 5 .w raster lines (profiling) */
        .equ    rt_lcache,      RT_VARS+0xD0    /* 2 x (.w x, .w y, .w crop, .w -): last positions written per layer */
        .equ    rt_boot,        RT_VARS+0x7F    /* .b start-up progress (debugging) */
        .equ    rt_err_pc,      RT_VARS+0x80    /* .l trap_error */
        .equ    rt_err_sr,      RT_VARS+0x84    /* .w */
        .equ    rt_err_regs,    RT_VARS+0x88    /* 16 .l */
        .equ    rt_io_sp,       RT_VARS+0xE0    /* .l sp around SYSTEM_IO (MVS: the BIOS re-enters USER from it) */
        .equ    rt_bios_cred,   RT_VARS+0xE4    /* .b MVS credits (BCD) last seen */
        .equ    rt_coins_due,   RT_VARS+0xE5    /* .b coin pulses still to give the arcade */
        .equ    rt_coin_seq,    RT_VARS+0xE6    /* .b frame of the current coin pulse */
        .equ    rt_prev_sel,    RT_VARS+0xE7    /* .b SELECT last frame */
        .equ    rt_reinit,      RT_VARS+0xE8    /* .b the BIOS took the screen: rebuild the video */
        .equ    rt_fskip,       RT_VARS+0xE9    /* .b frameskip level 0-3 (video.s) */
        .equ    rt_ytabkey,     RT_VARS+0xEA    /* .w rt_mode / rt_pcrop YTAB was built for */
        .equ    rt_fsk_n,       RT_VARS+0xEC    /* .b frames skipped in a row */
        .equ    rt_gframe,      RT_VARS+0xED    /* .b the game started a frame since the last VBlank (rt_newframe) */
        .equ    RT_CLEAR_END,   RT_VARS+0x100
        .equ    backup_area,    RT_VARS+0x700   /* 256 bytes the BIOS saves to backup RAM (MVS) */

* runtime buffers
        .equ    ENT_SH,         RT_BUF          /* 511 x 4 words: each arcade sprite entry last frame */
        .equ    ENT_FIX,        ENT_SH+511*8    /* 511 words: fix VRAM address the entry's text is in, 0 = none */
        .equ    FIX_CLRQ,       ENT_FIX+511*2   /* fix addresses to blank at the end of the frame (511 words) */
        .equ    YTAB,           FIX_CLRQ+511*2  /* 256 words: SCB3 word of each arcade y byte (video.s ytab_sync) */
        .equ    RT_BUF_END,     YTAB+512        /* < BIOS_RAM */
        .equ    FIX_STAMP,      RT_VARS+0x100   /* 1280 bytes: frame id of the last text written (by fix offset) */
        .equ    RT_COLSLOT,     RT_VARS+0x600   /* 2 x 32 bytes: column sprite of each map column (0xFF none) */
        .equ    RT_PALMARK,     RT_VARS+0x640   /* 64 bytes: palettes the game's copy routine wrote (rt_palcopy) */
        .equ    FIX_BLANK,      0x00FF

* Neo Geo sprite allocation (= priority, higher on top)
        .equ    SPR_TM1,        1               /* 21 column sprites: tilemap 1 (back) */
        .equ    SPR_TM0,        22              /* 21 column sprites: tilemap 0 */
        .equ    SPR_ARC,        43              /* arcade sprites: entry k = SPR_MAX-1-k */
        .equ    SPR_MAX,        360
        .equ    SPR_HI,         360             /* 21 column sprites above them: tilemap 0's priority-1 cells */
        .equ    HI_TILES,       0x14000         /* + code: the tilemap tile with pens 1-7 only (gfx2crom.py) */
        .equ    HI_BLANK,       0x18000
        .equ    NCOL,           21

* Neo Geo palettes: 16..79 = arcade palettes 0..63; 0..15 = copies of arcade 48..63 for the text on the
* fix layer (the font uses colours 0x30-0x3F); PAL_FIX = the runtime's own error screen
        .equ    PAL_FIX,        0
        .equ    PAL_ARC,        16
        .equ    PAL_TEXT,       48

* screen: the arcade shows 320x240 (rows 16-255), the Neo Geo 320x224.  Intro / title / menus: arcade rows
* 32-255 (the intro's last line of text is at 240-255).  Game (something in the top HUD band, rows 16-31):
* the playfield shows rows 24-247, the top HUD (rows 16-31) comes down 8 lines and the bottom HUD (rows
* 232-255) goes up 8, both at 1:1 over the edges of the playfield
        .equ    CROP_Y,         32

* P2 ROM (bank 0, 0x200000): decryption tables (tools/mkcrypt.py)
        .equ    CT_T0,          0x200000
        .equ    CT_TP,          0x220000
        .equ    CT_BS,          0x240000
        .equ    CT_KR,          0x241000
        .equ    CT_BF,          0x241200
        .equ    CT_PAL,         0x241600
        .equ    CT_MAP8,        0x251600        /* 8x8 sprite code -> Neo Geo tile - 0x8000 (tools/gfx2crom.py) */
        .equ    CT_SHADOW,      0x280000        /* + 2 x: decrypt_long of the ROM long at x (code area, mkrom.py) */
        .equ    CT_MOVEP,       0x2F0000        /* [a1 - 0x80000]: decrypted movep.l (a1) of the map data (mkrom.py) */
        .equ    MOVEP_LO,       0x80000
        .equ    MOVEP_N,        0x8000
        .equ    CT_HI0,         0x2D0000        /* [e2]: low word of decrypt_long(0000:e2) (mkrom.py) */

* arcade program
        .equ    ARC_IRQ6,       0x010CFA        /* arcade VBlank handler (vector 30) */
        .equ    ARC_RESUME,     0x000410        /* after the reset prologue patched by tools/mkrom.py */
