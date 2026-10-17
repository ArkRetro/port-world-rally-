* main.s - BIOS entry points, interrupt handlers and the frame loop of the Neo Geo runtime.
* Conventions: everything runs in supervisor mode (like the BIOS leaves us), interrupts at level 0 in
* the main loop; the VBlank handler does only bookkeeping (SYSTEM_IO, watchdog, frame flag).

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global USER, PLAYER_START, DEMO_END, COIN_SOUND
        .global irq_vblank, irq_timer, trap_error
        .global wait_vblank

* ------------------------------------------------------------------------------------------
* USER: called by the BIOS with SR=2700, cart vectors enabled, BIOS_USER_REQUEST = request.
* ------------------------------------------------------------------------------------------
USER:
        move    #0x2700,sr
        lea     BIOS_RAM,sp
        /* the track data lives in the second megabyte of the P ROM, seen through the banked window at
           0x200000: on a real cartridge / NeoSD the bank register need not be 0 at power-on */
        move.w  #0,REG_P2BANK
        moveq   #0,d0
        move.b  BIOS_USER_REQUEST,d0
        /* MVS: a coin during the demo makes the BIOS re-enter USER (request 3 "title") from inside
           SYSTEM_IO.  The arcade logic is still intact: resume our VBlank handler where it was. */
        tst.b   rt_arcade_on
        beq     1f
        cmpi.b  #2,d0
        bcs     1f
        move.l  rt_io_sp,sp
        bra     irq_after_io
1:
        cmpi.b  #3,d0
        bhi.s   user_init
        add.w   d0,d0
        move.w  user_tab(pc,d0.w),d0
        jmp     user_tab(pc,d0.w)
user_tab:
        .word   user_init-user_tab
        .word   user_eyecatcher-user_tab
        .word   user_demo-user_tab
        .word   user_title-user_tab

user_init:
        /* request 0: cold start. Clear our RAM, nothing else; the BIOS continues with its own screens */
        bsr     rt_clear_ram
        move.b  #0,BIOS_USER_MODE
        jmp     SYSTEM_RETURN

user_eyecatcher:
        /* request 1: we claimed to draw the eyecatcher ourselves (header 0x114 = 1) - skip it */
        jmp     SYSTEM_RETURN

user_demo:
user_title:
        /* requests 2 (demo) and 3 (title after a coin, MVS): run the game from the top */
        move.b  #1,BIOS_USER_MODE
        bsr     rt_clear_ram
        bsr     video_init
        bsr     sound_init
        jmp     game_main               /* never returns; game_main enables interrupts when ready */

* PLAYER_START: BIOS saw START with credits during USER_MODE 1 (MVS) or on AES.
* BIOS_PLAYER_MOD1/2 already say who starts. We only raise a flag for the game loop.
PLAYER_START:
        move.b  #2,BIOS_USER_MODE
        st      rt_start_req
        rts

* DEMO_END: BIOS wants the demo to end (coin inserted on MVS). Nothing to do: the game loop
* notices the credit through SYSTEM_IO variables.
DEMO_END:
        rts

* COIN_SOUND: BIOS asks for a coin sound (MVS)
COIN_SOUND:
        rts

* ------------------------------------------------------------------------------------------
* Interrupts
* ------------------------------------------------------------------------------------------
irq_vblank:
        btst    #7,BIOS_SYSTEM_MODE
        bne.s   1f
        jmp     SYSTEM_INT1             /* BIOS owns the screen (menus, how-to-play...) */
1:      movem.l d0-d7/a0-a6,-(sp)
        move.w  REG_LSPCMODE,rt_irq_line /* profiling: raster line at the VBlank interrupt entry */
        move.w  #4,REG_IRQACK
        move.b  d0,REG_WATCHDOG
        addq.l  #1,rt_frame
        st      rt_vbl_flag
        bsr     vbl_stats
        tst.b   rt_in_hook
        beq.s   1f
        /* a tick is still running (it was allowed to be interrupted): its palettes, layers and sprites are
           half prepared, so nothing is shown from them - the screen keeps the last complete frame (showing
           part of a tick mixed sprite and layer positions of two frames); the next VBlank shows it */
        addq.w  #1,rt_skip
        cmpi.b  #4,rt_mw_age            /* the VBlank's tick is owed to the main loop all the same */
        bcc.s   3f
        cmpi.b  #2,rt_tick_pend
        bcc.s   3f
        addq.b  #1,rt_tick_pend
3:      jsr     arc_sound_tick
        movem.l (sp)+,d0-d7/a0-a6
        rte
1:      /* first of all, while the beam is in the vertical blank: palettes and the VRAM queue of the last
           tick (and the map changes the main loop made since) */
        bsr     video_vblank
        bsr     vram_flush
        move.w  REG_LSPCMODE,rt_dbg+12  /* profiling: raster line at the end of the VRAM flush */
        move.l  sp,rt_io_sp
        jsr     SYSTEM_IO               /* inputs, coins, credits; may re-enter USER (see above) */
irq_after_io:
        jsr     snd_vblank
        jsr     arc_sound_tick          /* the arcade sound sequencer keeps the 60 Hz of the music */
        cmpi.b  #255,rt_mw_age
        beq.s   3f
        addq.b  #1,rt_mw_age
3:      /* the arcade main loop is in the middle of a frame (it waited for a tick less than 4 VBlanks ago and
           is not waiting now): hold this tick back, main_wait runs it as soon as the frame is finished.  The
           arcade's interrupt work (race clock, sprite lists built from the objects, scroll) thus always
           follows a complete frame of its main loop, as on the arcade where the loop fits in a frame: the
           clock no longer runs ahead of the car and the sprites never mix two frames */
        cmpi.b  #4,rt_mw_age
        bcc.s   4f
        /* each VBlank gives the main loop one tick (two at most are kept: after a long frame the next one
           starts at once, so the average speed comes back without ever going above 60 ticks a second) */
        cmpi.b  #2,rt_tick_pend
        bcc.s   5f
        addq.b  #1,rt_tick_pend
5:      tst.b   rt_mwait
        beq.s   6f                      /* in the middle of a frame: main_wait will run it */
        subq.b  #1,rt_tick_pend
        bra.s   7f
6:      movem.l (sp)+,d0-d7/a0-a6
        rte
4:      clr.b   rt_tick_pend            /* the main loop is not in its frame loop: one tick per VBlank */
7:      st      rt_in_hook
        /* the frame work (arcade IRQ, layers, sprites) can last longer than a frame: the next VBlank may
           interrupt it to flush the VRAM in the blanking (otherwise it would be written during the display) */
        move    #0x2000,sr
        jsr     frame_hook              /* game: arcade IRQ + uploads; viewer: rts */
        move    #0x2100,sr
        clr.b   rt_in_hook
        movem.l (sp)+,d0-d7/a0-a6
        rte

* vbl_stats: once a second, the arcade ticks run and the idle time of its main loop (diagnostic overlay),
* in BCD so that the overlay's hex digits read as decimal.  Trashes d0-d1.
vbl_stats:
        subq.b  #1,rt_st_cnt
        bpl.s   9f
        move.b  #59,rt_st_cnt
        move.l  rt_ticks,d0
        move.l  d0,d1
        sub.l   rt_st_ticks,d0
        move.l  d1,rt_st_ticks
        bsr.s   to_bcd
        move.w  d0,rt_tps
        move.l  rt_idle,d0
        move.l  d0,d1
        sub.l   rt_st_idle,d0
        move.l  d1,rt_st_idle
        cmpi.l  #2253*100,d0
        bls.s   1f
        move.l  #2253*100,d0
1:      divu    #2253,d0                /* main_wait iterations (54 cycles) per second -> % of 12.17 Mcycles */
        and.l   #0xFFFF,d0
        bsr.s   to_bcd
        move.w  d0,rt_idlepct
9:      rts
to_bcd: /* d0.l (0..999) -> d0.w BCD */
        cmpi.l  #999,d0
        bls.s   1f
        move.w  #999,d0
1:      and.l   #0xFFFF,d0
        divu    #100,d0
        move.w  d0,d1                   /* hundreds */
        clr.w   d0
        swap    d0
        divu    #10,d0                  /* tens, units in the high word */
        lsl.w   #4,d1
        or.w    d0,d1
        swap    d0
        lsl.w   #4,d1
        or.w    d0,d1
        move.w  d1,d0
        rts

irq_timer:
        btst    #7,BIOS_SYSTEM_MODE
        bne.s   1f
        jmp     SYSTEM_INT2
1:      move.w  #2,REG_IRQACK
        rte

* Wait for the next VBlank (frame flag set by the handler)
wait_vblank:
        clr.b   rt_vbl_flag
1:      tst.b   rt_vbl_flag
        beq.s   1b
        rts

* ------------------------------------------------------------------------------------------
* TRAP #n / fatal error: show registers on the fix layer and spin, kicking the watchdog.
* ------------------------------------------------------------------------------------------
trap_error:
        move    #0x2700,sr
        movem.l d0-d7/a0-a7,rt_err_regs
        move.l  2(sp),rt_err_pc
        move.w  (sp),rt_err_sr
        bsr     video_init_min
        lea     err_msg(pc),a0
        moveq   #2,d0                   /* x */
        moveq   #3,d1                   /* y */
        bsr     fix_print
        moveq   #2,d0
        moveq   #5,d1
        move.l  rt_err_pc,d2
        bsr     fix_hex32
        moveq   #12,d0
        moveq   #5,d1
        moveq   #0,d2
        move.w  rt_err_sr,d2
        bsr     fix_hex16
        lea     rt_err_regs,a1
        moveq   #7,d1                   /* row */
        moveq   #15,d3
2:      moveq   #2,d0
        move.l  (a1)+,d2
        movem.l d1/d3/a1,-(sp)
        bsr     fix_hex32
        movem.l (sp)+,d1/d3/a1
        addq.w  #1,d1
        dbra    d3,2b
3:      move.b  d0,REG_WATCHDOG
        bra.s   3b
err_msg:
        .asciz  "WR-NG TRAP  PC       SR"
        .align  2

* Clear the runtime RAM (0x100000 .. RT_CLEAR_END; the stack above it is not touched)
rt_clear_ram:
        lea     RAM_BASE,a0
        move.w  #(RT_CLEAR_END-RAM_BASE)/4-1,d0
        moveq   #0,d1
1:      move.l  d1,(a0)+
        dbra    d0,1b
        rts
