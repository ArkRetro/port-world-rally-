* neogeo.i - Neo Geo hardware / BIOS equates (GNU as, Motorola syntax)
* Sources: neogeodev wiki conventions, verified against MAME 0.264 neogeo.cpp and the sp-s2 BIOS disassembly.

* ---- Memory map ----
        .equ    RAM_BASE,       0x100000
        .equ    BIOS_RAM,       0x10F300        /* BIOS work RAM start (do not touch below here except documented vars) */
        .equ    BIOS_BASE,      0xC00000

* ---- I/O ----
        .equ    REG_P1CNT,      0x300000        /* byte read: P1 controls, active low (U D L R A B C D) */
        .equ    REG_DIPSW,      0x300001        /* byte read: hardware dips (MVS); byte write: watchdog kick */
        .equ    REG_WATCHDOG,   0x300001
        .equ    REG_SYSTYPE,    0x300081
        .equ    REG_SOUND,      0x320000        /* byte write: command to Z80 (NMI); byte read: Z80 reply */
        .equ    REG_STATUS_A,   0x320001        /* byte read: coins / service / calendar (MVS) */
        .equ    REG_P2CNT,      0x340000        /* byte read: P2 controls */
        .equ    REG_STATUS_B,   0x380000        /* byte read: start/select, MVS/AES bit 6, memcard */
        .equ    REG_POUTPUT,    0x380001
        .equ    REG_CRDBANK,    0x380011
        .equ    REG_P2BANK,     0x2FFFF0        /* P ROM bank register (cartridge, 0x200000 window) */
        .equ    REG_SLOT,       0x380021
        .equ    REG_LEDLATCHES, 0x380031
        .equ    REG_LEDDATA,    0x380041
        .equ    REG_RTCCTRL,    0x380051
        .equ    REG_RESETCC1,   0x380061
        .equ    REG_RESETCC2,   0x380063
        .equ    REG_RESETCL1,   0x380065
        .equ    REG_RESETCL2,   0x380067
        .equ    REG_SETCC1,     0x3800E1
        .equ    REG_SETCC2,     0x3800E3
        .equ    REG_SETCL1,     0x3800E5
        .equ    REG_SETCL2,     0x3800E7

* ---- System registers (byte writes, value ignored) ----
        .equ    REG_NOSHADOW,   0x3A0001
        .equ    REG_SWPBIOS,    0x3A0003        /* vectors 0x00-0x7F from BIOS */
        .equ    REG_CRDUNLOCK1, 0x3A0005
        .equ    REG_CRDNORMAL,  0x3A0009
        .equ    REG_BRDFIX,     0x3A000B        /* fix layer / Z80 ROM from BIOS */
        .equ    REG_SRAMLOCK,   0x3A000D
        .equ    REG_PALBANK1,   0x3A000F
        .equ    REG_SHADOW,     0x3A0011
        .equ    REG_SWPROM,     0x3A0013        /* vectors from cartridge */
        .equ    REG_CRDLOCK1,   0x3A0015
        .equ    REG_CRDLOCK2,   0x3A0019
        .equ    REG_CRTFIX,     0x3A001B        /* fix layer / Z80 ROM from cartridge */
        .equ    REG_SRAMUNLOCK, 0x3A001D
        .equ    REG_PALBANK0,   0x3A001F

* ---- LSPC (video) ----
* VRAM_GAP: during active display the LSPC grants the 68k few access slots; SNK's rules are at least 16 CPU
* cycles between a data write and the next address write (12 between data writes), else writes are lost
* (not emulated by MAME).  Four nops = 16 cycles.
        .macro  VRAM_GAP
        nop
        nop
        nop
        nop
        .endm

        .equ    REG_VRAMADDR,   0x3C0000
        .equ    REG_VRAMRW,     0x3C0002
        .equ    REG_VRAMMOD,    0x3C0004
        .equ    REG_LSPCMODE,   0x3C0006
        .equ    REG_TIMERHIGH,  0x3C0008
        .equ    REG_TIMERLOW,   0x3C000A
        .equ    REG_IRQACK,     0x3C000C
        .equ    REG_TIMERSTOP,  0x3C000E

        .equ    VRAM_SCB1,      0x0000          /* 64 words per sprite: tile, attr */
        .equ    VRAM_FIX,       0x7000          /* 40 columns x 32 rows, column major */
        .equ    VRAM_SCB2,      0x8000          /* shrink */
        .equ    VRAM_SCB3,      0x8200          /* y (496-y)<<7 | sticky<<6 | height */
        .equ    VRAM_SCB4,      0x8400          /* x<<7 */

        .equ    PALETTES,       0x400000        /* 256 palettes x 16 colours x 2 bytes */

* ---- BIOS calls ----
        .equ    SYSTEM_INT1,    0xC00438
        .equ    SYSTEM_INT2,    0xC0043E
        .equ    SYSTEM_RETURN,  0xC00444
        .equ    SYSTEM_IO,      0xC0044A
        .equ    CREDIT_CHECK,   0xC00450
        .equ    CREDIT_DOWN,    0xC00456
        .equ    READ_CALENDAR,  0xC0045C
        .equ    SETUP_CALENDAR, 0xC00462
        .equ    CARD,           0xC00468
        .equ    CARD_ERROR,     0xC0046E
        .equ    HOW_TO_PLAY,    0xC00474
        .equ    CHECKSUM,       0xC0047A
        .equ    FIX_CLEAR,      0xC004C2
        .equ    LSP_1ST,        0xC004C8
        .equ    MESS_OUT,       0xC004CE
        .equ    CONTROLLER_SETUP, 0xC004D4

* ---- BIOS exception handlers (cart vector table entries 2..9 point here) ----
        .equ    BIOS_VEC_BUSERR,   0xC00408
        .equ    BIOS_VEC_ADDRERR,  0xC0040E
        .equ    BIOS_VEC_ILLEGAL,  0xC00414
        .equ    BIOS_VEC_DIV0,     0xC0041A
        .equ    BIOS_VEC_CHK,      0xC00420
        .equ    BIOS_VEC_TRAPV,    0xC00426
        .equ    BIOS_VEC_PRIV,     0xC0042C
        .equ    BIOS_VEC_TRACE,    0xC00432

* ---- BIOS RAM variables ----
        .equ    BIOS_SYSTEM_MODE,  0x10FD80     /* bit 7: 0 = BIOS owns the VBlank, 1 = game */
        .equ    BIOS_MVS_FLAG,     0x10FD82     /* 0 = AES, 1 = MVS */
        .equ    BIOS_COUNTRY_CODE, 0x10FD83     /* 0 Japan, 1 USA, 2 Europe */
        .equ    BIOS_GAME_DIP,     0x10FD84     /* 16 bytes of soft dips */
        .equ    BIOS_P1STATUS,     0x10FD94
        .equ    BIOS_P1PREVIOUS,   0x10FD95
        .equ    BIOS_P1CURRENT,    0x10FD96
        .equ    BIOS_P1CHANGE,     0x10FD97
        .equ    BIOS_P1REPEAT,     0x10FD98
        .equ    BIOS_P1TIMER,      0x10FD99
        .equ    BIOS_P2STATUS,     0x10FD9A
        .equ    BIOS_P2PREVIOUS,   0x10FD9B
        .equ    BIOS_P2CURRENT,    0x10FD9C
        .equ    BIOS_P2CHANGE,     0x10FD9D
        .equ    BIOS_P2REPEAT,     0x10FD9E
        .equ    BIOS_P2TIMER,      0x10FD9F
        .equ    BIOS_STATCURNT,    0x10FDAC     /* start/select current (active high) */
        .equ    BIOS_STATCHANGE,   0x10FDAD
        .equ    BIOS_USER_REQUEST, 0x10FDAE     /* 0 init, 1 eyecatcher, 2 demo, 3 title */
        .equ    BIOS_USER_MODE,    0x10FDAF     /* 0 init, 1 title/demo, 2 game */
        .equ    BIOS_CREDIT_DEC1,  0x10FDB0
        .equ    BIOS_CREDIT_DEC2,  0x10FDB1
        .equ    BIOS_START_FLAG,   0x10FDB4
        .equ    BIOS_PLAYER_MOD1,  0x10FDB6     /* 0 never played, 1 playing, 2 continue, 3 game over */
        .equ    BIOS_PLAYER_MOD2,  0x10FDB7
        .equ    BIOS_FRAME_COUNTER,0x10FE88     /* actually 0x10FE8A is the word the BIOS polls */
        .equ    BIOS_DEVMODE,      0x10FE80

* PAL_LATE label: branch if the beam is past the VBlank.  The real hardware shows dots ("snow") where the
* beam is when the palette RAM is accessed during the picture (MAME does not); the VBlank is raster lines
* 496-511 and 248-271, keep 8 lines for the rest.  Trashes d0.
        .macro  PAL_LATE label
        move.w  REG_LSPCMODE,d0
        lsr.w   #7,d0
        cmpi.w  #496,d0
        bcc.s   1000f
        cmpi.w  #264,d0
        bcc     \label
1000:
        .endm
