; World Rally NG - controlador de sonido Z80 (ADPCM-A), ensamblar con pasmo
; Protocolo (bit 7 = alternancia, se ignora):
;   0x01 / 0x03         ordenes de la BIOS
;   0x10-0x1F           parar canales (mascara en bits 0-3)
;   0x40-0x7F + frase   tocar: 01cc aaaa (canal cc, atenuacion OKI aaaa) seguido de byte de frase
; Respuesta: el mismo byte recibido en el puerto 0x0C.

        include "jingle.inc"
STATE   equ 0xF800
HDR     equ 0xF801
SEQ     equ 0xF802

        org 0
        di
        jp start

        org 0x38
        ei
        reti

        org 0x66
nmi:    push af
        push bc
        push de
        push hl
        in a,(0)
        ld b,a
        cp 1
        jp z,cmd01
        cp 3
        jp z,cmd03
        cp 2                    ; 0x02 de la BIOS: melodia de la intro Neo Geo
        jr nz,n2
        call jingle
        jr fin
n2:     bit 7,a
        jr nz,frase             ; 0x80-0xFF: numero de frase (solo si hay cabecera pendiente)
        cp 0x40
        jr nc,cabecera          ; 0x40-0x7F: cabecera (canal y atenuacion)
        cp 0x10
        jr c,fin
        cp 0x20
        jr nc,fin
        and 0x0F                ; 0x10-0x1F: parar canales
        call parar
        jr fin
cabecera:
        ld (HDR),a
        ld a,1
        ld (STATE),a
        jr fin
frase:  ld c,a
        ld a,(STATE)
        or a
        jr z,fin                ; frase sin cabecera: se ignora (nunca se desincroniza)
        xor a
        ld (STATE),a
        ld a,c
        and 0x7F
        call tocar
fin:    ld a,(SEQ)              ; respuesta: contador que cambia con cada byte recibido
        inc a
        or 0x80
        ld (SEQ),a
        out (0x0C),a
        pop hl
        pop de
        pop bc
        pop af
        retn

cmd01:  ld a,b
        out (0x0C),a
        pop hl
        pop de
        pop bc
        pop af
        retn

cmd03:  ld a,b
        out (0x0C),a
        ld sp,0xFFFE
        ld hl,start
        push hl
        retn

; ---- escritura YM2610 puerto B (registros ADPCM-A): d=reg e=valor
ymb:    in a,(4)
        rla
        jr c,ymb
        ld a,d
        out (6),a
        nop
        nop
        ld a,e
        out (7),a
        ret
; puerto A
yma:    in a,(4)
        rla
        jr c,yma
        ld a,d
        out (4),a
        nop
        nop
        ld a,e
        out (5),a
        ret

; parar canales: a = mascara (bit0 = canal 0)
parar:  or 0x80
        ld e,a
        ld d,0
        jp ymb

; tocar frase a (0-127) en canal (HDR>>4)&3 con atenuacion HDR&15
tocar:  ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        ld de,tabla
        add hl,de               ; hl -> inicio(lo,hi) fin(lo,hi)
        inc hl
        inc hl
        ld a,(hl)
        inc hl
        or (hl)
        dec hl
        dec hl
        dec hl
        ret z                   ; frase inexistente (fin 0; la frase 1 empieza en 0)
        push hl
        ld a,(HDR)
        rrca
        rrca
        rrca
        rrca
        and 3
        ld c,a                  ; c = canal
        ; mascara de canal en b
        ld b,1
        or a
        jr z,m_ok
m_lp:   sla b
        dec a
        jr nz,m_lp
m_ok:   ld a,b
        or 0x80                 ; key off canal
        ld e,a
        ld d,0
        call ymb
        ; volumen
        ld a,(HDR)
        and 0x0F
        ld e,a
        ld d,0
        ld hl,voltab
        add hl,de
        ld a,(hl)
        or 0xC0                 ; L+R
        ld e,a
        ld a,0x08
        add a,c
        ld d,a
        call ymb
        pop hl
        ld a,0x10               ; inicio bajo
        add a,c
        ld d,a
        ld e,(hl)
        call ymb
        inc hl
        ld a,0x18               ; inicio alto
        add a,c
        ld d,a
        ld e,(hl)
        call ymb
        inc hl
        ld a,0x20               ; fin bajo
        add a,c
        ld d,a
        ld e,(hl)
        call ymb
        inc hl
        ld a,0x28               ; fin alto
        add a,c
        ld d,a
        ld e,(hl)
        call ymb
        ld e,b                  ; key on
        ld d,0
        jp ymb

; melodia de la intro (canal 5 del ADPCM-A)
jingle: ld a,JS_LO
        or JS_HI
        ret z
        ld d,0x00
        ld e,0xA0               ; key off canal 5
        call ymb
        ld d,0x0D
        ld e,0xDF               ; L+R, nivel maximo
        call ymb
        ld d,0x15
        ld e,JS_LO
        call ymb
        ld d,0x1D
        ld e,JS_HI
        call ymb
        ld d,0x25
        ld e,JE_LO
        call ymb
        ld d,0x2D
        ld e,JE_HI
        call ymb
        ld d,0x00
        ld e,0x20               ; key on canal 5
        jp ymb

start:  di
        ld sp,0xFFFE
        im 1
        xor a
        ld (STATE),a
        ld (SEQ),a
        ; silenciar SSG (puerto A: mezclador y volumenes)
        ld d,0x07
        ld e,0x3F
        call yma
        ld d,0x08
        ld e,0
        call yma
        ld d,0x09
        call yma
        ld d,0x0A
        call yma
        ; ADPCM-B parado
        ld d,0x10
        ld e,0x01
        call yma
        ; ADPCM-A: todo parado, volumen total maximo
        ld d,0x00
        ld e,0xBF
        call ymb
        ld d,0x01
        ld e,0x3F
        call ymb
        ; habilitar NMI (escritura en puerto 0x08)
        out (0x08),a
bucle:  jr bucle

; atenuacion OKI -> nivel ADPCM-A (0-31, 0.75 dB por paso)
voltab: db 31,27,23,19,15,12,7,0,0,0,0,0,0,0,0,0

        org 0x400
tabla:  incbin "/home/claude/wrally/neo/build/snd_ztab.bin"
