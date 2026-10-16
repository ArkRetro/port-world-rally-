; driver.s - minimal Neo Geo Z80 sound driver (sdasz80 syntax)
;
; Protocol with the 68k (REG_SOUND 0x320000): a byte written by the 68k raises an NMI here, the byte is read
; from port 0 and acknowledged by writing port 0; replies go to port 0x0C.  The BIOS sends 1 (slot switch)
; and expects reply 1, and 3 (soft reset).  Everything else is ours (sound effects / music through ADPCM-A).
;
; YM2610 ports: 4/5 = address/data of bank A0, 6/7 = bank A1 (ADPCM-A registers live in A1).

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
        push    hl
        in      a,(0x00)
        out     (0x00),a                ; ack (clears the latch)
        ld      b,a
        cp      #0x01
        jr      z,cmd_slot
        cp      #0x03
        jr      z,cmd_reset
        or      a
        jr      z,nmi_done
        ; game command: 0x20.. = play ADPCM-A sample (cmd-0x20), 0x10 = stop all
        cp      #0x10
        jr      z,cmd_stop
        cp      #0x20
        jr      c,nmi_done
        sub     #0x20
        call    play_sample
        jr      nmi_done
cmd_slot:
        call    silence
        ld      a,#0x01
        out     (0x0C),a
        jr      nmi_done
cmd_reset:
        call    silence
        ld      a,#0x01
        out     (0x0C),a
        pop     hl
        pop     bc
        pop     af
        jp      start
cmd_stop:
        call    silence
nmi_done:
        pop     hl
        pop     bc
        pop     af
        retn

start:
        ld      a,#0x00
        out     (0x0C),a                ; reply 0 (idle)
        call    ym_init
        ; enable NMI (write to port 8), then idle
        ld      a,#0x00
        out     (0x08),a
idle:
        ei
        halt
        jr      idle

; ---- YM2610 helpers ----
; write register: a = reg, c = value, bank A1
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
        ; ADPCM-A: stop all, master volume max
        ld      a,#0x00
        ld      c,#0xBF
        call    ym_a1
        ld      a,#0x01
        ld      c,#0x3F
        call    ym_a1
        ; channel levels: max, both speakers
        ld      a,#0x08
        ld      c,#0xDF
        ld      b,#6
2$:     call    ym_a1
        inc     a
        djnz    2$
        ret

silence:
        ld      a,#0x00
        ld      c,#0xBF                 ; dump all ADPCM-A channels
        call    ym_a1
        ret

; play_sample: a = sample number (index into the table at samples:), channel = a mod 6
play_sample:
        ld      d,#0
        ld      e,a
        ld      hl,#samples
        add     hl,de
        add     hl,de
        add     hl,de
        add     hl,de                   ; hl = samples + 4*a
ps1:    cp      #6
        jr      c,ps2
        sub     #6
        jr      ps1
ps2:    ld      b,a                     ; b = channel 0..5
        ld      d,#1                    ; d = 1 << channel
        inc     a
ps3:    dec     a
        jr      z,ps4
        sla     d
        jr      ps3
ps4:    ld      a,d
        or      #0x80
        ld      c,a
        ld      a,#0x00
        call    ym_a1                   ; dump (stop) the channel
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
        ld      c,d
        ld      a,#0x00
        call    ym_a1                   ; key on
        ret

; sample table: start_lo, start_hi, end_lo, end_hi (addresses / 256) - filled by the build
samples:
        .include "samples.inc"
