* viewer.s - test program: shows frames dumped from the arcade (VRAM, vregs, palette, sprites)
* through the port's video engine.  A/B/C/D + START select the frame.  Not part of the final game.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global game_main, sound_init, frame_hook

sound_init:
frame_hook:
        rts

game_main:
        move    #0x2000,sr              /* VBlank on */
        moveq   #0,d6                   /* current frame index */
        bsr     load_frame
main_loop:
        bsr     wait_vblank
        /* select a dump with A/B/C/D (BIOS_P1CHANGE: pressed this frame) */
        move.b  BIOS_P1CHANGE,d0
        moveq   #-1,d1
        btst    #4,d0
        bne.s   1f
        moveq   #1,d1
        btst    #5,d0
        bne.s   1f
        moveq   #2,d1
        btst    #6,d0
        bne.s   1f
        moveq   #3,d1
        btst    #7,d0
        bne.s   1f
        moveq   #4,d1
        move.b  BIOS_STATCHANGE,d0
        btst    #0,d0
        bne.s   1f
        bra.s   2f
1:      addq.w  #1,d1
        move.w  d1,d6
        bsr     load_frame
2:      bsr     video_frame
        /* status line */
        moveq   #1,d0
        moveq   #1,d1
        move.l  rt_frame,d2
        bsr     fix_hex32
        moveq   #10,d0
        moveq   #1,d1
        moveq   #0,d2
        move.w  d6,d2
        bsr     fix_hex16
        bra     main_loop

* load_frame: d6 = index 0..4
load_frame:
        move.w  d6,d0
        mulu    #5*4,d0
        lea     frame_table,a6
        adda.w  d0,a6
        /* vram 16 KB */
        move.l  (a6)+,a0
        lea     ARC_VRAM,a1
        move.w  #0x4000/4-1,d0
1:      move.l  (a0)+,(a1)+
        dbra    d0,1b
        /* vregs */
        move.l  (a6)+,a0
        lea     ARC_VREGS,a1
        move.l  (a0)+,(a1)+
        move.l  (a0)+,(a1)+
        move.l  (a0)+,(a1)+
        move.l  (a0)+,(a1)+
        /* palette: 1024 entries through pal_write */
        move.l  (a6)+,a2
        moveq   #0,d5
2:      move.w  d5,d0
        move.w  (a2)+,d1
        bsr     pal_write
        addq.w  #1,d5
        cmpi.w  #1024,d5
        bcs.s   2b
        /* sprites 4 KB */
        move.l  (a6)+,a0
        lea     ARC_SPR,a1
        move.w  #0x1000/4-1,d0
3:      move.l  (a0)+,(a1)+
        dbra    d0,3b
        bsr     video_mark_all_dirty
        rts

        .align  2
frame_table:
        .long   vram_0, vregs_0, pal_0, spr_0, 0
        .long   vram_1, vregs_1, pal_1, spr_1, 0
        .long   vram_2, vregs_2, pal_2, spr_2, 0
        .long   vram_3, vregs_3, pal_3, spr_3, 0
        .long   vram_4, vregs_4, pal_4, spr_4, 0

vram_0: .incbin "dumps/vram_900.bin"
vregs_0: .incbin "dumps/vregs_900.bin"
pal_0:  .incbin "dumps/pal_900.bin",0,2048
spr_0:  .incbin "dumps/spr_900.bin"
vram_1: .incbin "dumps/vram_1500.bin"
vregs_1: .incbin "dumps/vregs_1500.bin"
pal_1:  .incbin "dumps/pal_1500.bin",0,2048
spr_1:  .incbin "dumps/spr_1500.bin"
vram_2: .incbin "dumps/vram_2100.bin"
vregs_2: .incbin "dumps/vregs_2100.bin"
pal_2:  .incbin "dumps/pal_2100.bin",0,2048
spr_2:  .incbin "dumps/spr_2100.bin"
vram_3: .incbin "dumps/vram_2700.bin"
vregs_3: .incbin "dumps/vregs_2700.bin"
pal_3:  .incbin "dumps/pal_2700.bin",0,2048
spr_3:  .incbin "dumps/spr_2700.bin"
vram_4: .incbin "dumps/vram_3300.bin"
vregs_4: .incbin "dumps/vregs_3300.bin"
pal_4:  .incbin "dumps/pal_3300.bin",0,2048
spr_4:  .incbin "dumps/spr_3300.bin"
        .align  2
