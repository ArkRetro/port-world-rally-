; driver.s - Neo Geo Z80 sound driver of the World Rally port (sdasz80 syntax)
;
; The arcade's OKI M6295 (4 ADPCM channels) is mapped on YM2610 ADPCM-A channels 0-3.  The 68k keeps
; running the arcade's own sound sequencer; only the OKI register writes are turned into commands:
;
;   command byte (REG_SOUND 0x320000, raises NMI here)
;     0x01  BIOS slot switch   -> silence, reply 0x01
;     0x03  BIOS soft reset    -> silence, restart
;     0x08  stop all channels
;     0x0C+ch (0x0C-0x0F)       stop channel ch
;     0x40 + sample (0x40-0x7B) play sample 0..59 (table at samples:) on the selected channel/volume
;     0x80 | ch<<4 | vol        select channel ch (0..3) and OKI volume vol (0..15) for the next play
;   reply byte (read by the 68k at 0x320000): 0x80 | ack | busy mask (bit n = channel n playing);
;   bit 6 (ack) toggles every time a command is taken, so the 68k can wait before sending the next
;   one (the latch holds a single byte).  After the BIOS slot-switch command the reply is 0x01.
;
; YM2610 ports: 4/5 = address/data of register bank A0, 6/7 = bank A1 (ADPCM-A registers).  The end-of-
; sample flags are read from port 6 (bits 0-5 = ADPCM-A channels) and reset through register 0x1C (A0).

        .module driver
        .area   CODE (ABS)

        .org    0x0000
reset:
        di
        im      1
        ld      sp,#0xFFFC
        jp      start

        .org    0x0038                  ; INT (YM2610 timers, unused)
        ei
        reti

        .org    0x0066                  ; NMI: command from the 68k
nmi:
        push    af
        push    bc
        push    de
        push    hl
        in      a,(0x00)
        out     (0x00),a                ; clears the latch
        ld      b,a
        ld      a,(ack)
        xor     #0x40
        ld      (ack),a
        ld      a,b
        cp      #0x01
        jr      z,cmd_slot
        cp      #0x03
        jr      z,cmd_reset
        cp      #0x08
        jr      z,cmd_stopall
        cp      #0x0C
        jr      c,nmi_done
        cp      #0x10
        jr      c,cmd_stopch
        cp      #0x40
        jr      c,nmi_done
        cp      #0x7C
        jr      nc,cmd_select
        sub     #0x40
        call    play_cmd
        jr      nmi_done
cmd_select:
        cp      #0xC0
        jr      nc,nmi_done
        and     #0x0F
        ld      (pend_vol),a
        ld      a,b
        rrca
        rrca
        rrca
        rrca
        and     #0x03
        ld      (pend_ch),a
        jr      nmi_done
cmd_slot:
        call    silence
        ld      a,#0x01
        out     (0x0C),a
        ld      a,#0xFF
        ld      (slot_mode),a
        jr      nmi_done
cmd_reset:
        call    silence
        pop     hl
        pop     de
        pop     bc
        pop     af
        jp      start
cmd_stopall:
        call    silence
        jr      nmi_done
cmd_stopch:
        sub     #0x0C                   ; channel
        call    stop_channel
nmi_done:
        call    reply
        pop     hl
        pop     de
        pop     bc
        pop     af
        retn

start:
        xor     a
        ld      (busy),a
        ld      (slot_mode),a
        ld      (ack),a
        ld      (pend_ch),a
        ld      (pend_vol),a
        ld      a,#0x80
        out     (0x0C),a                ; reply: idle, nothing busy
        call    ym_init
        xor     a
        out     (0x08),a                ; enable NMI (maskable interrupts stay disabled: the YM timers are unused)
main:
        ; poll the ADPCM-A end flags and keep the busy mask / reply up to date
        in      a,(0x06)
        and     #0x0F
        jr      z,main_reply
        ld      c,a                     ; channels that ended
        cpl
        ld      b,a
        ld      a,(busy)
        and     b
        ld      (busy),a
        ld      a,#0x1C                 ; reset those flags, then re-enable them (register 0x1C is in bank A0)
        call    ym_a0
        ld      c,#0x00
        ld      a,#0x1C
        call    ym_a0
main_reply:
        call    reply
        jr      main

; reply: 0x80 | ack | busy (nothing while the BIOS slot switch is pending)
reply:
        ld      a,(slot_mode)
        or      a
        ret     nz
        ld      a,(busy)
        or      #0x80
        ld      b,a
        ld      a,(ack)
        or      b
        out     (0x0C),a
        ret

; ---- YM2610 helpers ----
; write register a = value c in bank A1 / A0
ym_a1:
        push    af
        out     (0x06),a
        call    ym_wait
        ld      a,c
        out     (0x07),a
        call    ym_wait
        pop     af
        ret
ym_a0:
        push    af
        out     (0x04),a
        call    ym_wait
        ld      a,c
        out     (0x05),a
        call    ym_wait
        pop     af
        ret
ym_wait:
        push    bc
        ld      b,#8
1$:     djnz    1$
        pop     bc
        ret

ym_init:
        ld      a,#0x27                 ; stop timers A/B and clear their flags (the BIOS driver leaves them
        ld      c,#0x30                 ; running: a pending timer IRQ would otherwise stay asserted)
        call    ym_a0
        ld      a,#0x00
        ld      c,#0xBF                 ; dump all ADPCM-A channels
        call    ym_a1
        ld      a,#0x01
        ld      c,#0x3F                 ; ADPCM-A master volume max
        call    ym_a1
        ld      a,#0x08                 ; channel levels: max, both speakers
        ld      c,#0xDF
        ld      b,#6
2$:     call    ym_a1
        inc     a
        djnz    2$
        ld      a,#0x1C                 ; flags: all enabled (bank A0)
        ld      c,#0x00
        call    ym_a0
        ret

silence:
        ld      a,#0x00
        ld      c,#0xBF
        call    ym_a1
        xor     a
        ld      (busy),a
        ret

; stop_channel: a = channel 0..3
stop_channel:
        and     #0x03
        ld      b,a
        call    mask_of_b               ; d = 1 << channel
        ld      a,d
        or      #0x80
        ld      c,a
        ld      a,#0x00
        call    ym_a1                   ; dump channel
        ld      a,d
        cpl
        ld      b,a
        ld      a,(busy)
        and     b
        ld      (busy),a
        ret

; mask_of_b: b = channel -> d = 1 << b (b preserved)
mask_of_b:
        push    bc
        ld      d,#1
        inc     b
3$:     dec     b
        jr      z,4$
        sla     d
        jr      3$
4$:     pop     bc
        ret

; play_cmd: a = sample (0..59), channel and volume from the last select command
play_cmd:
        cp      #60
        ret     nc
        ld      e,a
        ld      a,(pend_ch)
        ld      b,a
        ld      a,e
        ld      d,#0
        ld      hl,#samples
        add     hl,de
        add     hl,de
        add     hl,de
        add     hl,de                   ; hl -> start lo, start hi, end lo, end hi
        ld      a,(hl)
        inc     hl
        or      (hl)
        dec     hl
        ret     z                       ; empty entry: nothing to play
        call    mask_of_b               ; d = 1 << channel
        ld      a,d
        or      #0x80
        ld      c,a
        ld      a,#0x00
        call    ym_a1                   ; dump (stop) the channel first
        ld      a,#0x10
        add     a,b
        ld      c,(hl)
        inc     hl
        call    ym_a1                   ; start low
        ld      a,#0x18
        add     a,b
        ld      c,(hl)
        inc     hl
        call    ym_a1                   ; start high
        ld      a,#0x20
        add     a,b
        ld      c,(hl)
        inc     hl
        call    ym_a1                   ; end low
        ld      a,#0x28
        add     a,b
        ld      c,(hl)
        call    ym_a1                   ; end high
        ld      a,(pend_vol)            ; OKI attenuation -> instrument level + pan
        ld      e,a
        ld      d,#0
        ld      hl,#voltab
        add     hl,de
        ld      c,(hl)
        ld      a,#0x08
        add     a,b
        call    ym_a1
        call    mask_of_b               ; d = 1 << channel (hl/de clobbered above)
        ld      a,#0x1C                 ; reset + re-enable the end flag of this channel (bank A0)
        ld      c,d
        call    ym_a0
        ld      a,#0x1C
        ld      c,#0x00
        call    ym_a0
        ld      a,(busy)
        or      d
        ld      (busy),a
        ld      c,d
        ld      a,#0x00
        call    ym_a1                   ; key on
        ret

; OKI M6295 volume nibble (0 = 0 dB, 1 = -3.2 dB ... 8 = -24 dB, 9-15 = mute) -> ADPCM-A register 0x08+ch:
; bits 7-6 pan L/R, bits 4-0 instrument level in 0.75 dB steps (0x1F = 0 dB); mute = both pans off.
; One level per OKI step (0, -3.2, -6, -9.2, -12, -14.5, -18, -21, -24 dB -> 31 - dB/0.75), as in the
; arkretro port: the relative balance of music and effects is the arcade's (the samples carry +8.3 dB of
; gain with a soft limiter instead, tools/oki2adpcma.py, to match the arcade level).
voltab:
        .db 0xDF,0xDB,0xD7,0xD3,0xCF,0xCC,0xC7,0xC3,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00

; sample table: start_lo, start_hi, end_lo, end_hi (addresses / 256), generated by tools/oki2adpcma.py
samples:
        .include "samples.inc"

        .area   DATA (ABS)
        .org    0xF800
busy:           .ds 1
slot_mode:      .ds 1
ack:            .ds 1                   ; 0x00 / 0x40, toggled per command taken
pend_ch:        .ds 1                   ; channel selected for the next play
pend_vol:       .ds 1                   ; OKI volume nibble selected for the next play
