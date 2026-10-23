* main.s - BIOS entry points, interrupts and the start of the arcade program (Thunder Hoop port).
* The arcade program runs natively: its main loop in the foreground, its VBlank handler (0x10CFA) called
* at the end of ours, once per Neo Geo frame.  Everything runs in supervisor mode.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global USER, PLAYER_START, DEMO_END, COIN_SOUND
        .global irq_vblank, irq_timer, trap_error, group0_error, rt_reset, rt_fixcal, rt_cksum, rt_sprdone, rt_m24_d0, rt_m24_d1, rt_trap1, rt_palcopy
        .global rt_pal_a0, rt_pal_a1, rt_pal_a2, rt_pal_a3, rt_pal_a4, rt_pal_a5, rt_pal_a6

* USER: called by the BIOS with SR=2700, BIOS_USER_REQUEST = request
USER:
        move    #0x2700,sr
        lea     BIOS_RAM,sp
        move.w  #0,REG_P2BANK           /* the tables live in P2 bank 0 */
        moveq   #0,d0
        move.b  BIOS_USER_REQUEST,d0
        tst.b   rt_started              /* MVS coin: the BIOS re-enters USER; the arcade keeps running */
        beq.s   1f
        cmpi.b  #2,d0
        bcs.s   1f
        bra     user_resume
1:      cmpi.b  #2,d0
        bcc.s   user_game
        tst.b   d0
        bne.s   2f
        bsr     rt_clear_ram            /* request 0: cold start */
2:      move.b  #0,BIOS_USER_MODE
        jmp     SYSTEM_RETURN

user_game:
        /* requests 2 (demo) and 3 (title): run the arcade program from its reset vector */
        move.b  #1,BIOS_USER_MODE
        bsr     rt_clear_ram
        bsr     arc_clear_ram
        move.b  #1,rt_boot
        jsr     video_init
        move.b  #2,rt_boot
        jsr     snd_init
        move.b  #3,rt_boot
        bsr     io_init
        st      rt_started
        jmp     0x400                   /* arcade reset (prologue patched: rt_reset) */

user_resume:
        /* back from a BIOS screen: give the frame back to the game */
        move.b  #1,BIOS_USER_MODE
        lea     ARC_STACK-0x400,sp
        move    #0x2000,sr
3:      bra.s   3b

* arcade reset prologue (0x406, patched): SR, stack in the relocated work RAM, then 0x40E
rt_reset:
        move    #0x2700,sr
        move.b  #4,rt_boot
        lea     ARC_STACK,sp
        move.l  d0,-(sp)
        jmp     ARC_RESUME

* main loop head (0x4AE, patched): the arcade measured how many loop turns fit between two of its
* VBlanks (57.42 Hz) and checks the count later (protection, 0x8988: 0xDB1..0xDD1).  Keep it in range.
rt_fixcal:
        move.l  #0xDC1,ARC_WRAM+0xDB62-0x8000
        lea     ARC_WRAM,a5
        rts

* ROM checksum (0x1EC08, patched): the arcade sums the bytes 0x400-0x7FFFE at boot (shown in the test
* screen, kept at FFE558); the port's patches change it, so the original sum (tools/mkrom.py) is stored
rt_cksum:
        move.l  #0x12345678,d4          /* (immediate filled in by mkrom.py) */
        move.l  d4,ARC_WRAM+0xE558-0x8000
        rts

* end of the arcade's sprite list update (0x106D2, patched): the list changed, the runtime converts it
* at the next VBlank.  d3 = entries used before (the original instruction)
rt_sprdone:
        st      rt_sprdirty
        move.w  ARC_WRAM+0xD2D8-0x8000,d3
        rts

* the object scripts' 24-bit pointers (andi.l #$FFFFFF,dn in the interpreter, patched): RAM ones into
* the relocated work RAM; the flags of the andi
rt_m24_d0:
        andi.l  #0xFFFFFF,d0
        cmpi.l  #0xFF8000,d0
        bcs.s   1f
        subi.l  #0xFF8000-ARC_WRAM,d0
1:      tst.l   d0
        rts
rt_m24_d1:
        andi.l  #0xFFFFFF,d1
        cmpi.l  #0xFF8000,d1
        bcs.s   1f
        subi.l  #0xFF8000-ARC_WRAM,d1
1:      tst.l   d1
        rts

* TRAP #1: the arcade's switch (0x506) over the inline table of encrypted code pointers after the trap,
* index d0.  The arcade writes the entry to screen RAM and reads it back decrypted; here it comes from the
* decrypted shadow (the screen RAM word is still written, decrypted, as the game may look at it)
rt_trap1:
        move.l  a0,-(sp)
        lsl.w   #2,d0
        movea.l 6(sp),a0                /* table = the return address */
        adda.w  d0,a0
        adda.l  a0,a0
        adda.l  #CT_SHADOW,a0
        movea.l (a0),a0
        move.l  a0,ARC_SCREEN+0x040
        clr.l   d0                      /* 0x522: what the arcade leaves in d0 */
        move.l  d3,d0
        addi.w  #0xF00,d0
        asr.l   #6,d0
        move.l  a0,6(sp)
        movea.l (sp)+,a0
        rte

* lea $200000.l,an (the palette base, patched): the game is about to change palettes
        .irp    n,0,1,2,3,4,5,6
rt_pal_a\n:
        lea     ARC_PAL,a\n
        st      rt_paldirty
        rts
        .endr

* the game's palette copy (0x9188: palettes d0 .. d0+d2-1 from its RAM copy; lea $ffa5c0,a2 patched):
* mark them for pal_sync
rt_palcopy:
        lea     ARC_WRAM+0xA5C0-0x8000,a2
        movem.l d0/d2/a0,-(sp)
        andi.w  #0xFF,d0
        lea     RT_PALMARK,a0
        bra.s   2f
1:      cmpi.w  #64,d0
        bcc.s   3f
        st      (a0,d0.w)
        addq.w  #1,d0
2:      dbra    d2,1b
3:      movem.l (sp)+,d0/d2/a0
        rts

PLAYER_START:
        move.b  #2,BIOS_USER_MODE
        rts
DEMO_END:
        rts
COIN_SOUND:
        rts

* ------------------------------------------------------------------------------------------
irq_vblank:
        btst    #7,BIOS_SYSTEM_MODE
        bne.s   1f
        jmp     SYSTEM_INT1
1:      movem.l d0-d7/a0-a6,-(sp)
        move.b  #5,rt_boot
        move.w  #4,REG_IRQACK
        move.b  d0,REG_WATCHDOG
        addq.l  #1,rt_frame
        st      rt_vblank
        move.w  REG_LSPCMODE,rt_prof    /* profiling: raster line at entry / after video / after IO */
        jsr     video_vblank
        move.w  REG_LSPCMODE,rt_prof+2
        jsr     SYSTEM_IO
        move.w  REG_LSPCMODE,rt_prof+4
        bsr     io_update
        jsr     snd_vblank
        movem.l (sp)+,d0-d7/a0-a6
        jmp     ARC_IRQ6                /* the arcade handler, ends with its rte */

irq_timer:
        btst    #7,BIOS_SYSTEM_MODE
        bne.s   1f
        jmp     SYSTEM_INT2
1:      move.w  #2,REG_IRQACK
        rte

* ------------------------------------------------------------------------------------------
* arcade inputs: DSW1 = free play (no coins on the Neo Geo side yet), DSW2 = defaults
io_init:
        move.w  #0xFFCF,IO_DSW2
        move.w  #0xFF7F,IO_DSW1
        move.w  #0xFFFF,IO_P1
        move.w  #0xFFFF,IO_P2
        rts

* Neo Geo joystick (active low: U D L R A B C D) -> arcade P1/P2 (active low: U D R L B2 B1, P1 bit 6-7 =
* coins, P2 bit 6-7 = START1/START2)
io_update:
        move.b  REG_P1CNT,d0
        bsr     joy_conv
        ori.w   #0xFFC0,d1
        move.w  d1,IO_P1
        move.b  REG_P2CNT,d0
        bsr     joy_conv
        ori.w   #0xFFC0,d1
        move.b  REG_STATUS_B,d0         /* bit 0 = P1 start, bit 2 = P2 start (active low) */
        btst    #0,d0
        bne.s   1f
        bclr    #6,d1
1:      btst    #2,d0
        bne.s   2f
        bclr    #7,d1
2:      move.w  d1,IO_P2
        rts

joy_conv:
        moveq   #-1,d1
        btst    #0,d0
        bne.s   1f
        bclr    #0,d1
1:      btst    #1,d0
        bne.s   2f
        bclr    #1,d1
2:      btst    #3,d0                   /* right */
        bne.s   3f
        bclr    #2,d1
3:      btst    #2,d0                   /* left */
        bne.s   4f
        bclr    #3,d1
4:      btst    #4,d0                   /* A -> button 1 */
        bne.s   5f
        bclr    #5,d1
5:      btst    #5,d0                   /* B -> button 2 */
        bne.s   6f
        bclr    #4,d1
6:      rts

* ------------------------------------------------------------------------------------------
* bus / address error: 8 more bytes on the stack (access type, address, instruction); shown in d0-d1 rows
group0_error:
        move    #0x2700,sr
        move.l  2(sp),rt_err_regs+64    /* fault address */
        addq.l  #8,sp
trap_error:
        move    #0x2700,sr
        movem.l d0-d7/a0-a7,rt_err_regs
        move.l  2(sp),rt_err_pc
        move.w  (sp),rt_err_sr
        jsr     video_init_min
        lea     err_msg(pc),a0
        moveq   #2,d0
        moveq   #3,d1
        jsr     fix_print
        moveq   #2,d0
        moveq   #5,d1
        move.l  rt_err_pc,d2
        jsr     fix_hex32
        moveq   #12,d0
        moveq   #5,d1
        moveq   #0,d2
        move.w  rt_err_sr,d2
        jsr     fix_hex16
        lea     rt_err_regs,a1
        moveq   #7,d1
        moveq   #16,d3                  /* d0-d7, a0-a7, fault address */
2:      moveq   #2,d0
        move.l  (a1)+,d2
        movem.l d1/d3/a1,-(sp)
        jsr     fix_hex32
        movem.l (sp)+,d1/d3/a1
        addq.w  #1,d1
        dbra    d3,2b
3:      move.b  d0,REG_WATCHDOG
        bra.s   3b
err_msg:
        .asciz  "TH-NG TRAP  PC       SR"
        .align  2

rt_clear_ram:
        lea     RT_VARS,a0
        move.w  #(RT_CLEAR_END-RT_VARS)/4-1,d0
        moveq   #0,d1
1:      move.l  d1,(a0)+
        dbra    d0,1b
        rts

* the arcade's RAM, VRAM, palette and sprites (everything below RT_VARS and 104000-10D86F)
arc_clear_ram:
        moveq   #0,d1
        lea     ARC_VRAM,a0
        move.w  #(RT_VARS-ARC_VRAM)/4-1,d0
1:      move.l  d1,(a0)+
        dbra    d0,1b
        lea     ARC_WRAM,a0
        move.w  #(RT_BUF-ARC_WRAM)/4-1,d0
2:      move.l  d1,(a0)+
        dbra    d0,2b
        rts
