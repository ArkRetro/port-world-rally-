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
        move.w  #4,REG_IRQACK
        move.b  d0,REG_WATCHDOG
        addq.l  #1,rt_frame
        st      rt_vbl_flag
        move.l  sp,rt_io_sp
        jsr     SYSTEM_IO               /* inputs, coins, credits; may re-enter USER (see above) */
irq_after_io:
        bsr     video_vblank            /* palette flush (must be in VBlank) */
        jsr     snd_vblank
        jsr     frame_hook              /* game: arcade IRQ + uploads; viewer: rts */
        movem.l (sp)+,d0-d7/a0-a6
        rte

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
