* header.s - cartridge vector table and Neo Geo program header.
* Vectors 2..24 and 27..31 are exactly the ones the SP-S2 BIOS installs in its own table, so a fault
* in game code is handled by the BIOS (clean reset to the BIOS screen) the same way as on a commercial game.
* Only VBlank (25) and the timer (26) are ours; TRAP vectors go to a visible error screen.

        .include "neogeo.i"
        .include "ram.i"

        .section .vectors,"ax"
        .global _start
_start:
        .long   BIOS_RAM                /* 0  initial SSP (0x10F300, like every commercial game)      */
        .long   0x00C00402              /* 1  reset -> BIOS                                           */
        .long   0x00C00408              /* 2  bus error                                               */
        .long   0x00C0040E              /* 3  address error                                           */
        .long   0x00C00414              /* 4  illegal instruction                                     */
        .long   0x00C00426              /* 5  divide by zero                                          */
        .long   0x00C00426              /* 6  chk                                                     */
        .long   0x00C00426              /* 7  trapv                                                   */
        .long   0x00C0041A              /* 8  privilege violation                                     */
        .long   0x00C00420              /* 9  trace                                                   */
        .long   0x00C00426              /* 10 line A                                                  */
        .long   0x00C00426              /* 11 line F                                                  */
        .long   0x00C00426, 0x00C00426, 0x00C00426                      /* 12-14 */
        .long   0x00C0042C              /* 15 uninitialised interrupt                                 */
        .long   0x00C00426, 0x00C00426, 0x00C00426, 0x00C00426          /* 16-19 */
        .long   0x00C00426, 0x00C00426, 0x00C00426, 0x00C00426          /* 20-23 */
        .long   0x00C00432              /* 24 spurious interrupt                                      */
        .long   irq_vblank              /* 25 level 1: VBlank                                         */
        .long   irq_timer               /* 26 level 2: LSPC timer                                     */
        .long   0x00C00426              /* 27 level 3                                                 */
        .long   0x00C00426, 0x00C00426, 0x00C00426, 0x00C00426          /* 28-31 */
        .long   trap_error              /* 32 TRAP #0 */
        .long   trap1_handler           /* 33 TRAP #1: arcade VRAM write hook (see tools/mkrom.py) */
        .rept   14
        .long   trap_error              /* 34-47 TRAP #2..#15 */
        .endr
        .rept   16
        .long   0x00C00426              /* 48-63 */
        .endr

* ---- Program header (0x100) ----
        .ascii  "NEO-GEO"
        .byte   0
        .word   0x1993                  /* 0x108 NGH number (0x999 is V-Liner's: Geolith gives it its board) */
        .long   0x100000                /* 0x10A program size                                          */
        .long   backup_area             /* 0x10E backup RAM work area pointer                         */
        .word   0x0100                  /* 0x112 backup RAM size (bytes)                              */
        .byte   1                       /* 0x114 eyecatcher: 1 = game draws it (eye.s)                */
        .byte   0                       /* 0x115 */
        .long   softdips_jp             /* 0x116 */
        .long   softdips_us             /* 0x11A */
        .long   softdips_eu             /* 0x11E */
        jmp     USER                    /* 0x122 */
        jmp     PLAYER_START            /* 0x128 */
        jmp     DEMO_END                /* 0x12E */
        jmp     COIN_SOUND              /* 0x134 */
        .fill   0x182-0x13A, 1, 0
        .long   security_code           /* 0x182 */
        .fill   0x1A0-0x186, 1, 0

* ---- Security code: the 122 bytes the BIOS compares word by word with its own copy ----
        .align  2
security_code:
        .incbin "seccode.bin"
        .align  2

* ---- Soft dips (all "unused" for now) ----
softdips_jp:
softdips_us:
softdips_eu:
        .ascii  "WORLD RALLY     "      /* 16 chars */
        .word   0xFFFF, 0xFFFF          /* time settings: none */
        .byte   0xFF, 0xFF              /* integer settings: none */
        .fill   10, 1, 0xFF             /* enumerated settings: none */
        .fill   12*14, 1, 0
        .align  2
