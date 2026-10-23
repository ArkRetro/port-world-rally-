* header.s - cartridge vector table and Neo Geo program header of the Thunder Hoop port.
* Faults go to the BIOS handlers (as on commercial games); line A is the hook of the arcade VRAM writes
* (src/crypt.s); the TRAP vectors are the arcade's own (its handlers stay where they are in the program).

        .include "neogeo.i"
        .include "ram.i"

        .section .vectors,"ax"
        .global _start
_start:
        .long   ARC_STACK               /* 0  initial SSP                                             */
        .long   0x00C00402              /* 1  reset -> BIOS                                           */
        .long   0x00C00408              /* 2  bus error                                               */
        .long   0x00C0040E              /* 3  address error                                           */
        .long   0x00C00414              /* 4  illegal instruction                                     */
        .long   0x00C00426              /* 5  divide by zero                                          */
        .long   0x00C00426              /* 6  chk                                                     */
        .long   0x00C00426              /* 7  trapv                                                   */
        .long   0x00C0041A              /* 8  privilege violation                                     */
        .long   0x00C00420              /* 9  trace                                                   */
        .long   linea_handler           /* 10 line A: arcade VRAM write site (crypt.s)                */
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
        /* 32-47: TRAP #0..#15, the arcade's vectors (#1 = switch over an encrypted inline table, #3 / #5
           take an inline word, #0, #6 and #8-#13 are its error stops; its #0 handler at 0x27C is under the
           header now) */
        .long   trap_error, 0x00000506, 0x00000534, 0x000005AA
        .long   0x0000062A, 0x00000646, trap_error, 0x000006AE
        .long   trap_error, trap_error, trap_error, trap_error
        .long   trap_error, trap_error, 0x0001A8C6, 0x0001A692
        .long   0xFBC00426              /* 48: the arcade reads its byte 0xC0 as region flags (0xFB = this */
        .rept   15                      /* set: Europe, no US warning screen); the CPU ignores the top byte */
        .long   0x00C00426              /* 49-63 */
        .endr

* ---- Program header (0x100) ----
        .ascii  "NEO-GEO"
        .byte   0
        .word   0x1992                  /* 0x108 NGH number                                            */
        .long   0x100000                /* 0x10A program size                                          */
        .long   backup_area             /* 0x10E backup RAM work area pointer                         */
        .word   0x0100                  /* 0x112 backup RAM size (bytes)                              */
        .byte   0                       /* 0x114 eyecatcher: 0 = the BIOS draws its own                */
        .byte   0                       /* 0x115 */
        .long   softdips                /* 0x116 */
        .long   softdips                /* 0x11A */
        .long   softdips                /* 0x11E */
        jmp     USER                    /* 0x122 */
        jmp     PLAYER_START            /* 0x128 */
        jmp     DEMO_END                /* 0x12E */
        jmp     COIN_SOUND              /* 0x134 */
        .fill   0x182-0x13A, 1, 0
        .long   security_code           /* 0x182 */
        .fill   0x1A0-0x186, 1, 0

        .align  2
security_code:
        .incbin "seccode.bin"
        .align  2

softdips:
        .ascii  "THUNDER HOOP    "      /* 16 chars */
        .word   0xFFFF, 0xFFFF
        .byte   0xFF, 0xFF
        .fill   10, 1, 0xFF
        .fill   12*14, 1, 0
        .align  2
