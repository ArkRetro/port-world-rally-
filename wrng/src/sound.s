* sound.s - OKI M6295 register writes of the arcade sound driver (0x14000-0x14444) turned into Z80 commands.
* The arcade sequencer keeps running unchanged; tools/mkrom.py patches its five OKI register writes with
* `jsr` to the hooks below (the registers are relocated into the I/O shadow, so nothing else touches them).
*
*   snd_hook_stopall  0x1402C  move.b #$78,$70000F   (init: stop the 4 channels)
*   snd_hook_stop0    0x1437A  move.b d0,$70000F     (d0 = 0x08<<channel mask, before (re)starting a phrase)
*   snd_hook_stop1    0x142A2  move.b d1,$70000F     (d1 = stop mask, when a channel is taken over)
*   snd_hook_bank     0x14386  move.b d4,$70000D     (OKI bank 1..15; bank 0 is never written - unused, see snd_hook_phrase)
*   snd_hook_phrase   0x14394  move.b d1,$70000F     (d1 = 0x80 | phrase)
*   snd_hook_play     0x143A8  move.b d1,$70000F     (d1 = (1<<channel)<<4 | volume  -> starts the phrase)
*
* The arcade reads the OKI status (bits 0-3 = channel busy) at 0x70000F.  As in the arkretro port it is
* emulated from each phrase's length (tools/oki2adpcma.py -> sample_dur.inc): set on play, counted down by
* snd_vblank, cleared by the stop hooks; a start on a busy channel is ignored, like the OKI does.
*
* Z80 protocol (z80/driver.s): every command byte is acknowledged by toggling bit 6 of the reply, and
* snd_send waits for that toggle (with a timeout) so two commands never collide in the sound latch.

        .include "neogeo.i"
        .include "ram.i"

        .text
        .global snd_hook_stopall, snd_hook_stop0, snd_hook_stop1, snd_hook_bank, snd_hook_phrase, snd_hook_play
        .global snd_vblank, snd_init, snd_send

        .equ    Z80_STOPALL,    0x08
        .equ    Z80_STOPCH,     0x0C
        .equ    Z80_PLAY,       0x40            /* + phrase slot 0x00-0x3F; slots 0x40-0x6F use 0xC0 + n */
        .equ    Z80_SELECT,     0x80            /* | channel<<4 | OKI volume nibble */
        .equ    SND_TIMEOUT,    500             /* snd_send spins (about 1.5 ms, only if the Z80 is dead) */

snd_init:
        clr.b   rt_okibank
        clr.b   rt_okiphrase
        clr.b   IO_OKI
        /* the BIOS leaves its own Z80 program running; after REG_CRTFIX (video_init) restart the Z80
           in our M1 with the soft-reset command and give it time to initialise */
        move.b  #3,REG_SOUND
        move.w  #20000,d0
1:      move.b  d0,REG_WATCHDOG
        dbra    d0,1b
        rts

* snd_send: d0.b = command; waits for the Z80's ack toggle (bit 6 of the reply)
snd_send:
        movem.l d1-d2,-(sp)
        move    sr,-(sp)
        ori     #0x0700,sr              /* the main loop and the VBlank both send: one command at a time */
        move.b  REG_SOUND,d1
        and.b   #0x40,d1
        move.b  d0,REG_SOUND
        move.w  #SND_TIMEOUT,d2
1:      move.b  REG_SOUND,d0
        and.b   #0x40,d0
        cmp.b   d0,d1
        dbne    d2,1b
        move    (sp)+,sr
        movem.l (sp)+,d1-d2
        rts

snd_hook_stopall:
        move.l  d0,-(sp)
        move.b  #Z80_STOPALL,d0
        bsr     snd_send
        move.l  (sp)+,d0
        clr.b   IO_OKI
        clr.l   rt_oki_cnt
        clr.l   rt_oki_cnt+4
        rts

* d0 / d1 = OKI stop command byte (channels in bits 3-6)
snd_hook_stop0:
        movem.l d0-d1,-(sp)
        move.b  d0,d1
        bsr     snd_stop_mask
        movem.l (sp)+,d0-d1
        rts
snd_hook_stop1:
        movem.l d0-d1,-(sp)
        bsr     snd_stop_mask
        movem.l (sp)+,d0-d1
        rts
snd_stop_mask:
        movem.l d2/a0,-(sp)
        lsr.b   #3,d1
        and.w   #0x0F,d1
        moveq   #0,d2
1:      btst    d2,d1
        beq     2f
        bclr    d2,IO_OKI
        move.w  d2,d0
        add.w   d0,d0
        lea     rt_oki_cnt,a0
        clr.w   (a0,d0.w)
        move.b  d2,d0
        add.b   #Z80_STOPCH,d0
        bsr     snd_send
2:      addq.w  #1,d2
        cmpi.w  #4,d2
        bcs     1b
        movem.l (sp)+,d2/a0
        rts

snd_hook_bank:
        move.b  d4,rt_okibank
        rts

* d1 = 0x80 | phrase, d4 = bank byte of the same table entry (0 = fixed area, the bank register is not
* written then, so d4 rather than the last bank written is the key into the table)
snd_hook_phrase:
        move.b  d4,rt_okibank
        move.b  d1,rt_okiphrase
        rts

* d1 = (1<<channel)<<4 | volume
snd_hook_play:
        movem.l d0-d4/a0,-(sp)
        move.b  d1,d4                   /* volume nibble in the low bits */
        move.b  d1,d3
        lsr.b   #4,d3                   /* d3 = one-hot channel (1,2,4,8) */
        moveq   #0,d0
        move.b  d3,d2
1:      lsr.b   #1,d2
        beq     2f
        addq.w  #1,d0
        bra     1b
2:      and.w   #3,d0                   /* d0 = channel 0..3 */
        /* phrase -> slot (tools/oki2adpcma.py converts every phrase of the OKI ROM, its bank follows from
           the phrase number): not only the 60 pairs of the arcade's table are played - some script opcodes
           send a phrase number directly (coin, start countdown) */
        moveq   #0x7F,d2
        and.b   rt_okiphrase,d2         /* phrase */
        cmpi.b  #0x40,d2
        bcs     4f                      /* 0x00-0x3F: slot = phrase */
        cmpi.b  #0x50,d2
        bcs     9f                      /* 0x40-0x4F: not defined in the ROM */
        sub.b   #0x10,d2                /* 0x50-0x7F: slot 0x40-0x6F */
4:      move.b  IO_OKI,d1
        and.b   d3,d1
        bne     9f                      /* the OKI ignores a start on a channel that is playing */
        move.w  d2,d1
        add.w   d1,d1
        lea     sample_dur,a0
        move.w  (a0,d1.w),d1            /* busy for the phrase's length */
        add.w   d0,d0
        lea     rt_oki_cnt,a0
        move.w  d1,(a0,d0.w)
        lsr.w   #1,d0
        lsl.b   #4,d0                   /* select channel + volume, then play */
        and.b   #0x0F,d4
        or.b    d4,d0
        or.b    #Z80_SELECT,d0
        bsr     snd_send
        move.b  d2,d0
        add.b   #Z80_PLAY,d0            /* slot 0x00-0x3F -> 0x40-0x7F */
        cmpi.b  #0x80,d0
        bcs     5f
        add.b   #0x40,d0                /* slot 0x40-0x6F -> 0xC0-0xEF */
5:      bsr     snd_send
        or.b    d3,IO_OKI
9:      movem.l (sp)+,d0-d4/a0
        rts
sample_dur:
        .include "sample_dur.inc"

* snd_vblank: the arcade's OKI status byte (bit n = channel n busy) from the phrase lengths, as the
* arkretro port does: it does not depend on the YM2610's end flags, which the real chip and MAME do not
* report alike
snd_vblank:
        movem.l d0-d2/a0,-(sp)
        lea     rt_oki_cnt,a0
        moveq   #0,d1                   /* busy mask */
        moveq   #0,d2                   /* channel */
1:      move.w  (a0),d0
        beq.s   2f
        subq.w  #1,d0
        move.w  d0,(a0)
        beq.s   2f
        bset    d2,d1
2:      addq.l  #2,a0
        addq.w  #1,d2
        cmpi.w  #4,d2
        bcs.s   1b
        move.b  d1,IO_OKI
        movem.l (sp)+,d0-d2/a0
        rts
