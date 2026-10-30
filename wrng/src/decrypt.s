* decrypt.s - Gaelco VRAM "encryption" (World Rally: param1 = 0x1F, param2 = 0x522A), 68000 port of
* MAME's gaelcrpt.cpp (see analysis/gaelcrypt.py for the reference implementation).
* The arcade keeps tile pairs encrypted in ROM and the video hardware decrypts them on write; the game
* then reads the decrypted words back (pointers, attributes).  Bulk data is decrypted at build time;
* this routine serves the few single-shot "oracle" sites (see tools/mkrom.py).
*
* decrypt_long: d0.l = encrypted pair (hi = first word written) -> d0.l = decrypted pair.
* Preserves every other register.  Slow (~1500 cycles) but only used a few times per screen.

        .text
        .global decrypt_long

        .equ    P1,     0x1F
        .equ    P2,     0x522A

decrypt_long:
        movem.l d1-d7/a0-a1,-(sp)
        move.l  d0,d7                   /* d7 = enc pair */
        move.l  d0,d3
        swap    d3                      /* d3.w = enc_hi */
        moveq   #0,d1                   /* enc_prev = 0 */
        moveq   #0,d2                   /* dec_prev = 0 */
        bsr     decrypt16
        move.w  d0,d6                   /* d6.w = dec_hi */
        move.w  d0,d2                   /* dec_prev = dec_hi */
        move.l  d7,d1
        swap    d1                      /* d1.w = enc_prev = enc_hi */
        move.w  d7,d3                   /* enc_lo */
        bsr     decrypt16
        swap    d6
        move.w  d0,d6
        move.l  d6,d0
        movem.l (sp)+,d1-d7/a0-a1
        rts

* KBIT src,srcbit,dstbit: if bit srcbit of src is set, set bit dstbit of d4
        .macro  KBIT src, srcbit, dstbit
        btst    #\srcbit,\src
        beq   9f
        bset    #\dstbit,d4
9:
        .endm

* decrypt16: d1.w = enc_prev, d2.w = dec_prev, d3.w = enc -> d0.w.  Trashes d4, d5, a0, a1.
decrypt16:
        moveq   #0,d4
        KBIT    d2,8,1
        KBIT    d2,7,0                  /* d4 = swap */
        lsl.w   #4,d4
        lea     swap_tabs,a1
        adda.w  d4,a1
        moveq   #0,d5
        move.l  d5,d4
        KBIT    d2,12,1
        KBIT    d2,2,0
        move.w  d4,d5                   /* d5 = type */
        move.w  d3,d0
        movea.l a1,a0
        bsr     bitswap16
        eor.w   #P2,d0
        /* first k */
        moveq   #0,d4
        cmpi.w  #1,d5
        beq   k1
        cmpi.w  #2,d5
        beq   k2
        cmpi.w  #3,d5
        beq   k3
        moveq   #0x3A,d4                /* type 0: 0b111010 */
        bra   kdone
k1:     KBIT    d2,0,0
        KBIT    d2,1,1
        KBIT    d2,1,2
        KBIT    d1,3,3
        KBIT    d1,8,4
        KBIT    d1,15,5
        bra   kdone
k2:     KBIT    d1,5,0
        KBIT    d2,5,1
        KBIT    d1,7,2
        KBIT    d1,3,3
        KBIT    d1,13,4
        KBIT    d1,14,5
        bra   kdone
k3:     KBIT    d1,0,0
        KBIT    d1,9,1
        KBIT    d1,6,2
        KBIT    d2,4,3
        KBIT    d1,2,4
        KBIT    d2,11,5
kdone:
        eor.w   #P1,d4
        /* res = (res & 0xFFC0) | ((res + k) & 0x3F) */
        move.w  d0,-(sp)
        add.w   d4,d0
        and.w   #0x3F,d0
        and.w   #0xFFC0,(sp)
        or.w    (sp)+,d0
        eor.w   #P1,d0
        /* second k */
        moveq   #0,d4
        cmpi.w  #1,d5
        beq   q1
        cmpi.w  #2,d5
        beq   q2
        cmpi.w  #3,d5
        beq   q3
        KBIT    d3,9,0
        KBIT    d0,2,1
        KBIT    d3,5,2
        KBIT    d0,5,3
        KBIT    d0,4,4
        bra   qdone
q1:     KBIT    d2,2,0
        KBIT    d1,4,1
        KBIT    d2,14,2
        KBIT    d0,1,3
        KBIT    d2,12,4
        bra   qdone
q2:     KBIT    d1,6,0
        KBIT    d2,6,1
        KBIT    d2,15,2
        KBIT    d0,0,3
        KBIT    d2,7,4
        bra   qdone
q3:     KBIT    d2,2,0
        KBIT    d2,9,1
        KBIT    d1,5,2
        KBIT    d2,1,3
        KBIT    d1,10,4
qdone:
        eor.w   #P1,d4
        /* res = (res & 0x3F) | ((res + (k<<6)) & 0x7C0) | ((res + (k<<11)) & 0xF800) */
        move.w  d4,d5
        lsl.w   #6,d5
        add.w   d0,d5
        and.w   #0x07C0,d5
        lsl.w   #5,d4
        lsl.w   #6,d4
        add.w   d0,d4
        and.w   #0xF800,d4
        and.w   #0x003F,d0
        or.w    d5,d0
        or.w    d4,d0
        eor.w   #0xFFC0,d0              /* ^= (P1<<6)|(P1<<11) */
        lea     final_tab,a0
        bra     bitswap16

* bitswap16: d0.w = value, a0 = 16-byte table (source bit of result bits 15..0) -> d0.w
bitswap16:
        movem.l d4-d6,-(sp)
        move.w  d0,d4
        moveq   #0,d0
        moveq   #15,d5
1:      moveq   #0,d6
        move.b  (a0)+,d6
        btst    d6,d4
        beq   2f
        bset    d5,d0
2:      dbra    d5,1b
        movem.l (sp)+,d4-d6
        rts

swap_tabs:
        .byte   1,2,0,14,12,15,4,8,13,7,3,6,11,5,10,9
        .byte   14,10,4,15,1,6,12,11,8,0,9,13,7,3,5,2
        .byte   2,13,15,1,12,8,14,4,6,0,9,5,10,7,3,11
        .byte   3,8,1,13,14,4,15,0,10,2,7,12,6,11,9,5
final_tab:
        .byte   2,6,0,11,14,12,7,10,5,4,8,3,9,1,13,15
        .align  2
