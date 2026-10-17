| World Rally -> Neo Geo : cabecera, arranque y ganchos (GNU as 68000)
        .section .text.header,"ax"
        .ascii  "NEO-GEO"
        .byte   0
        .word   0x0999                  | NGH
        .long   0x00200000              | tamano programa
        .long   0x0010E000              | backup RAM (no usada)
        .word   0x0000
        .ifdef EYEC
        .byte   0                       | eye catcher: la BIOS dibuja la intro Neo Geo
        .byte   0xC0                    | con los tiles 0xC000-0xC0FF del C-ROM
        .else
        .byte   2                       | eye catcher: ninguno
        .byte   0
        .endif
        .long   softdip
        .long   softdip
        .long   softdip
        jmp     user_p1                 | 0x122 USER
        jmp     nothing_p1              | 0x128 PLAYER_START
        jmp     nothing_p1              | 0x12E DEMO_END
        jmp     nothing_p1              | 0x134 COIN_SOUND
        .fill   70,1,0xff
        .word   0x0000
        .org 0x82
        .long   security
security:
        .long 0x76004A6D,0x0A146600,0x003C206D,0x0A043E2D,0x0A0813C0,0x00300001
        .long 0x32100C01,0x00FF671A,0x30280002,0xB02D0ACE,0x66103028,0x0004B02D
        .long 0x0ACF6606,0xB22D0AD0,0x67085088,0x51CFFFD4,0x36074E75,0x206D0A04
        .long 0x3E2D0A08,0x3210E049,0x0C0100FF,0x671A3010,0xB02D0ACE,0x66123028
        .long 0x0002E048,0xB02D0ACF,0x6606B22D,0x0AD06708,0x588851CF,0xFFD83607
        .word 0x4E75
softdip:
        .ascii  "WORLD RALLY     "
        .word   0xFFFF,0xFFFF
        .byte   0xFF,0xFF
        .byte   0,0,0,0,0,0,0,0,0,0
| Todo nuestro codigo y sus tablas estan en el primer mega de la ROM P (ver build.py); en el segundo
| (0x200000, ventana con bancos) solo quedan los mapas descifrados (maptab). En un cartucho real
| (NeoSD) el registro de banco no tiene por que valer 0 al arrancar: se fija aqui.
        .even
user_p1:
        move.b  %d0,0x300001            | watchdog
        move.w  #0,0x2FFFF0             | banco 0 de P2
        jmp     user_entry
nothing_p1:
        rts

        .text
        .globl  crash_2, crash_3, crash_4, crash_x
| depuracion: excepciones -> guarda numero y marco en 0x10BF10 y se queda parado (pateando watchdog)
crash_2: move.w #2,0x10BF10
        bra.s   crash_grp0
crash_3: move.w #3,0x10BF10
crash_grp0:
        move.l  (%sp),0x10BF12
        move.l  4(%sp),0x10BF16
        move.l  8(%sp),0x10BF1A
        move.l  12(%sp),0x10BF1E
        bra.s   crash_loop
crash_4: move.w #4,0x10BF10
        bra.s   crash_g1
crash_x: move.w #99,0x10BF10
crash_g1:
        move.l  (%sp),0x10BF12
        move.l  4(%sp),0x10BF16
crash_loop:
        move.b  %d0,0x300001
        bra.s   crash_loop
        .globl  user_entry, lineA_handler, vblank_irq, irq_ignore, mcu_hook, vram_fix_a0, vram_fixp_a0
user_entry:
        move.b  %d0,0x300001            | watchdog
        cmpi.b  #2,0x10FDAE             | BIOS_USER_REQUEST: 0 init, 1 eye catcher -> volver a la BIOS
        bcc.s   1f
        jmp     0xC00444                | SYSTEM_RETURN
1:      move.w  #0x2700,%sr
        lea     0x10FF80,%sp
        jsr     ng_init
        move.w  #0x5752,0x10BFFE        | marca: juego en marcha (vblank propio)
        jmp     0x002400                | arranque original del juego
nothing:
        rts
irq_ignore:
        move.w  #1,0x3C000C
        rte
| IRQ2 temporizador del LSPC: vaciar la cola de sonido
timer_irq:
        move.w  #2,0x3C000C
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        lea     snd_pump,%a1
        bsr     onpriv
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        rte

| llama a la funcion C (a1) con argumentos d0,d1 en una pila propia:
| el juego guarda datos justo debajo de su pila y nuestro codigo C la necesita mas profunda
        .equ    PRIV_LO, 0x108AC0        | RAM libre entre los registros de video del juego (0x108000) y sus sprites (0x109000)
        .equ    PRIV_TOP, 0x108F00
onpriv:
        movea.l %sp,%a0
        cmpa.l  #PRIV_LO,%sp
        bcs.s   1f
        cmpa.l  #PRIV_TOP,%sp
        bls.s   2f
1:      lea     PRIV_TOP,%sp
2:      move.l  %a0,-(%sp)
        move.l  %d1,-(%sp)
        move.l  %d0,-(%sp)
        jsr     (%a1)
        addq.l  #8,%sp
        movea.l (%sp)+,%sp
        rts
        .globl  timer_irq

| Linea A: 0xAnnn sustituye a una escritura cifrada a VRAM; nnn = indice del stub
lineA_handler:
        move.b  %d0,0x300001            | watchdog: el juego escribe VRAM en bucles largos con IRQ desactivadas
        move.l  %a0,-(%sp)
        move.l  %d0,-(%sp)
        movea.l 10(%sp),%a0             | PC de la instruccion 0xAnnn
        move.w  (%a0),%d0
        andi.w  #0x0FFF,%d0
        lsl.w   #2,%d0
        lea     stub_table,%a0
        move.l  (%a0,%d0.w),10(%sp)     | volver (rte) al stub con SR original
        move.l  (%sp)+,%d0
        movea.l (%sp)+,%a0
        rte

| tile animado del recorrido 0x9E00 (scan_hook): a0 = VRAM del juego, d0.w = desplazamiento de la
| celda (capa 0), a1 = tabla del juego (0xF01FC / 0xF05FC), d1.w = indice*4. Equivale a
| "move.l (a1,d1.w),(a0,d0.w)" + vram_fix_a0, pero con el valor ya descifrado (tabla de la ROM):
| sin pasar por el descifrado, que aqui fallaba casi siempre en la cache
        .globl  scan_hit
scan_hit:
        movem.l %d0-%d5/%a0-%a3,-(%sp)
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        lea     scan_dec,%a2
        cmpa.l  #0xF01FC,%a1
        beq.s   1f
        lea     1024(%a2),%a2
1:      move.l  (%a2,%d1.w),%d3
        move.l  %d3,(%a0,%d0.w)
        move.w  %d3,%d1
        swap    %d3
        bsr     .Vhave
        move.w  (%sp)+,%sr
        movem.l (%sp)+,%d0-%d5/%a0-%a3
        rts

| a0 = direccion del long recien escrito (cifrado) -> descifra en sitio
vram_fix_a0:
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr             | la cola de cambios la vacia la IRQ de VBLANK: actualizar sin interrupciones
        move.l  %a0,%d0
        subi.l  #0x100000,%d0
        cmpi.l  #0x4000,%d0
        bcc     .Vslow0
        btst    #1,%d0
        bne     .Vslow0
        .ifdef PROF
        move.w  #3,0x10BFF2
        .endif
        movem.l %d2-%d5/%a2-%a3,-(%sp)
        bsr     .Vcore
        movem.l (%sp)+,%d2-%d5/%a2-%a3
        .ifdef PROF
        move.w  #4,0x10BFF2
        .endif
        move.w  (%sp)+,%sr
        rts
| nucleo: a0 = direccion del long en la VRAM del juego, d0 = desplazamiento; usa d0-d5/a0-a3
.Vcore:
        | cache de descifrado (256 entradas: clave e0:e1, valor d0:d1)
        move.l  (%a0),%d4
        move.l  %d4,%d5
        swap    %d5
        move.w  %d5,%d2
        lsl.w   #3,%d2
        sub.w   %d5,%d2
        eor.w   %d4,%d2
        andi.w  #0x7F,%d2
        lsl.w   #3,%d2
        lea     dcache,%a2
        adda.w  %d2,%a2
        cmp.l   (%a2),%d4
        bne.s   .Vmiss
        move.w  4(%a2),%d3
        move.w  6(%a2),%d1
        move.w  %d3,(%a0)
        move.w  %d1,2(%a0)
        bra     .Vhave
.Vmiss: move.l  %a2,-(%sp)
        moveq   #0,%d2
        move.w  (%a0),%d2               | e0
        add.l   %d2,%d2
        lea     dec_first,%a2
        move.w  (%a2,%d2.l),%d3         | primera palabra descifrada
        lea     gd_info,%a2
        move.w  (%a2,%d2.l),%d4         | info de descifrado de la segunda
        move.w  2(%a0),%d1              | e1
        movea.w %d1,%a3
        | res = sw[swap][e1 >> 8] | sw[swap][256 + (e1 & 0xff)]
        move.w  %d4,%d2
        andi.w  #3,%d2
        moveq   #10,%d5
        lsl.w   %d5,%d2
        lea     sw_tab,%a2
        adda.w  %d2,%a2
        lea     512(%a2),%a1
        moveq   #0,%d2
        move.w  %d1,%d2
        lsr.w   #8,%d2
        add.w   %d2,%d2
        move.w  (%a2,%d2.w),%d2
        andi.w  #0xFF,%d1
        add.w   %d1,%d1
        or.w    (%a1,%d1.w),%d2
        eori.w  #0x522a,%d2
        | res = (res & 0xffc0) | ((res + k1) & 0x3f); res ^= 0x1f
        move.w  %d4,%d1
        lsr.w   #4,%d1
        add.w   %d2,%d1
        andi.w  #0x3f,%d1
        andi.w  #0xffc0,%d2
        or.w    %d1,%d2
        eori.w  #0x1f,%d2
        | k2
        move.w  %d4,%d5
        lsr.w   #8,%d5
        lsr.w   #2,%d5
        btst    #3,%d4
        bne.s   .Vt23
        btst    #2,%d4
        bne.s   .Vt1
        | tipo 0: k = e1.9 | res.2<<1 | e1.5<<2 | res.5<<3 | res.4<<4
        move.w  %a3,%d1
        move.w  %d1,%d5
        lsr.w   #8,%d5
        lsr.w   #1,%d5
        andi.w  #1,%d5
        lsr.w   #3,%d1
        andi.w  #4,%d1
        or.w    %d1,%d5
        move.w  %d2,%d1
        lsr.w   #1,%d1
        andi.w  #2,%d1
        or.w    %d1,%d5
        move.w  %d2,%d1
        lsr.w   #2,%d1
        andi.w  #8,%d1
        or.w    %d1,%d5
        move.w  %d2,%d1
        andi.w  #0x10,%d1
        or.w    %d1,%d5
        bra.s   .Vk
.Vt1:   move.w  %d2,%d1
        lsl.w   #2,%d1
        andi.w  #8,%d1
        or.w    %d1,%d5
        bra.s   .Vk
.Vt23:  btst    #2,%d4
        bne.s   .Vk
        move.w  %d2,%d1
        lsl.w   #3,%d1
        andi.w  #8,%d1
        or.w    %d1,%d5
.Vk:    eori.w  #0x1f,%d5
        | res = (res & 0x3f) | ((res + (k << 6)) & 0x7c0) | ((res + (k << 11)) & 0xf800)
        move.w  %d5,%d1
        lsl.w   #6,%d1
        add.w   %d2,%d1
        andi.w  #0x7c0,%d1
        moveq   #11,%d4
        lsl.w   %d4,%d5
        add.w   %d2,%d5
        andi.w  #0xf800,%d5
        andi.w  #0x3f,%d2
        or.w    %d1,%d2
        or.w    %d5,%d2
        eori.w  #0xffc0,%d2
        | final = fin[0][res >> 8] | fin[1][res & 0xff]
        lea     fin_tab,%a2
        lea     512(%a2),%a1
        moveq   #0,%d1
        move.w  %d2,%d1
        lsr.w   #8,%d1
        add.w   %d1,%d1
        move.w  (%a2,%d1.w),%d1
        andi.w  #0xFF,%d2
        add.w   %d2,%d2
        or.w    (%a1,%d2.w),%d1
        movea.l (%sp)+,%a2
        move.l  (%a0),(%a2)             | guardar en la cache
        move.w  %d3,4(%a2)
        move.w  %d1,6(%a2)
        move.w  %d3,(%a0)
        move.w  %d1,2(%a0)
.Vhave:
        | columna fuera de pantalla: solo la VRAM del juego (column_write lo rehace al entrar)
        lsr.w   #2,%d0                  | e = capa*0x800 + fila*64 + col
        move.w  %d0,%d2
        lsr.w   #5,%d2
        andi.w  #64,%d2
        move.w  %d0,%d4
        andi.w  #63,%d4
        or.w    %d4,%d2
        add.w   %d2,%d2                 | (capa*64 + col)*2
        lea     cb1,%a1
        tst.w   (%a1,%d2.w)
        beq     5f
        movea.w %d2,%a3
        move.w  #1,0x3C0004
        move.w  %d3,%d4
        andi.w  #0x3FFF,%d4             | tile
        moveq   #0,%d2
        move.b  %d1,%d2
        add.w   %d2,%d2
        lea     tattr_tab,%a1
        move.w  (%a1,%d2.w),%d5         | atributo
        move.w  %d0,%d2
        lsr.w   #5,%d2
        andi.w  #62,%d2
        movea.w %d2,%a0                 | fila*2
        move.w  %d0,%d3
        andi.w  #63,%d3                 | columna
        move.w  %a3,%d2
        cmpi.w  #0x800,%d0
        bcs.s   .Vl0
        | capa 1 (variante con el pen 0 opaco, si la hay)
        move.w  %d4,%d0
        add.w   %d0,%d0
        lea     alt1_tab,%a1
        move.w  (%a1,%d0.w),%d0
        beq.s   0f
        move.w  %d0,%d4
        addi.w  #0x7000,%d5
0:      lea     cb1,%a1
        move.w  (%a1,%d2.w),%d0
        beq.s   1f
        add.w   %a0,%d0
        move.w  %d0,0x3C0000
        move.w  %d4,0x3C0002
        move.w  %d5,0x3C0002
        lea     cb2,%a1
        move.w  (%a1,%d2.w),%d0
        beq.s   1f
        add.w   %a0,%d0
        move.w  %d0,0x3C0000
        move.w  %d4,0x3C0002
        move.w  %d5,0x3C0002
1:      bra     5f
.Vl0:   move.b  #0,vchg
        move.w  %a0,%d2                 | mapa de categoria 1 (c1mask[col], bit fila)
        add.w   %d2,%d2
        lea     rowbit,%a1
        move.l  (%a1,%d2.w),%d2
        move.w  %d3,%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        lea     c1mask,%a1
        adda.w  %d0,%a1
        move.l  (%a1),%d0
        btst    #5,%d1
        beq.s   1f
        or.l    %d2,%d0
        bra.s   2f
1:      not.l   %d2
        and.l   %d2,%d0
2:      cmp.l   (%a1),%d0
        beq.s   3f
        move.l  %d0,(%a1)
        move.b  #1,hi_dirty
        move.b  #1,vchg
        move.w  %a0,%d0
        add.w   %d0,%d0
        lea     rowbit,%a1
        move.l  (%a1,%d0.w),%d0
        or.l    %d0,hi_rows
3:      | mapa de tiles no vacios (columnas vacias de la capa 0 no ocupan sprites)
        move.w  %d3,%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        lea     l0mask,%a1
        adda.w  %d0,%a1
        move.w  %a0,%d2
        add.w   %d2,%d2
        lea     rowbit,%a2
        move.l  (%a2,%d2.w),%d2
        move.w  %d4,%d0
        lsr.w   #3,%d0
        lea     tile_empty_tab,%a2
        move.b  (%a2,%d0.w),%d0
        ror.b   %d4,%d0                 | bit (tile & 7) -> bit 0
        btst    #0,%d0
        bne.s   1f
        move.l  (%a1),%d0
        or.l    %d2,%d0
        bra.s   2f
1:      move.l  (%a1),%d0
        not.l   %d2
        and.l   %d2,%d0
2:      cmp.l   (%a1),%d0
        beq.s   3f
        move.l  %d0,(%a1)
        move.b  #1,hi_dirty
        move.w  %a0,%d0
        add.w   %d0,%d0
        lea     rowbit,%a1
        move.l  (%a1,%d0.w),%d0
        or.l    %d0,hi_rows
3:      btst    #5,%d1                  | variante alta: tile+0x4000 o vacio
        beq.s   4f
        move.w  %d4,%d1
        addi.w  #0x4000,%d1
        move.w  %d5,%d3
        bra.s   6f
4:      tst.b   vchg                    | ni antes ni ahora de categoria 1: la variante alta ya
        bne.s   7f                      | esta vacia, solo se escribe la capa 0
        move.w  %a3,%d2
        lea     cb1,%a1
        bsr.s   .Vput0L
        lea     cb2,%a1
        bsr.s   .Vput0L
        bra     5f
7:      move.w  vconst+6,%d1
        moveq   #0,%d3
6:      move.w  %a3,%d2
        lea     cb1,%a1
        bsr.s   .Vput0
        lea     cb2,%a1
        bsr.s   .Vput0
        bra     5f
.Vput0L: move.w (%a1,%d2.w),%d0
        beq.s   9f
        add.w   %a0,%d0
        move.w  %d0,0x3C0000
        move.w  %d4,0x3C0002
        move.w  %d5,0x3C0002
9:      rts
| a1 = tabla cb, d2 = indice*2, a0 = fila*2, d4/d5 = tile/atributo, d1/d3 = tile/atributo alto
.Vput0: move.w  (%a1,%d2.w),%d0
        beq.s   9f
        add.w   %a0,%d0
        move.w  %d0,0x3C0000
        move.w  %d4,0x3C0002
        move.w  %d5,0x3C0002
        add.w   vconst+10,%d0
        move.w  %d0,0x3C0000
        move.w  %d1,0x3C0002
        move.w  %d3,0x3C0002
9:      rts
5:      rts
.Vslow0:
        moveq   #0,%d1
        bra.s   1f
vram_fixp_a0:
        moveq   #1,%d1
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
1:      move.l  %a0,%d0
        lea     vram_fix,%a1
        bsr     onpriv
        move.w  (%sp)+,%sr
        rts
        .bss
        .even
        .globl  dcache
dcache: .skip   1024
        .section .rodata
        .even
rowbit:
        .long 1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x400,0x800,0x1000,0x2000,0x4000,0x8000
        .long 0x10000,0x20000,0x40000,0x80000,0x100000,0x200000,0x400000,0x800000
        .long 0x1000000,0x2000000,0x4000000,0x8000000,0x10000000,0x20000000,0x40000000,0x80000000
        .text
| vram_fixp_a0: igual pero el valor descifrado puede ser un puntero a RAM/paleta/sprites/E-S -> reubicar

| relleno completo de una capa con un valor cifrado constante (sustituye a las rutinas 0x11FDC/0x11FF2)
        .globl  fill_l0, fill_l1
fill_l1:
        movem.l %d1/%a1,-(%sp)
        move.l  #0x102000,%d0
        move.l  0xDF64C,%d1
        lea     vram_fill,%a1
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        bsr     onpriv
        move.w  (%sp)+,%sr
        movem.l (%sp)+,%d1/%a1
        lea     0x104000,%a0
        move.w  #0xFFFF,%d0
        rts
fill_l0:
        movem.l %d1/%a1,-(%sp)
        move.l  #0x100000,%d0
        move.l  0xDF650,%d1
        lea     vram_fill,%a1
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        bsr     onpriv
        move.w  (%sp)+,%sr
        movem.l (%sp)+,%d1/%a1
        lea     0x102000,%a0
        move.w  #0xFFFF,%d0
        rts

| test de RAM del original (0x2878) con el watchdog alimentado en cada palabra
        .globl  ram_test
ram_test:
1:      move.b  %d0,0x300001
        move.w  (%a0),%d1
        move.w  #0xAAAA,(%a0)
        cmpi.w  #0xAAAA,(%a0)
        beq.s   2f
        movea.l %a0,%a6
2:      move.w  #0x5555,(%a0)
        cmpi.w  #0x5555,(%a0)
        beq.s   3f
        movea.l %a0,%a6
3:      move.w  %d1,(%a0)+
        dbra    %d0,1b
        rts

| ---------------------------------------------------------------------------------------
| copia de mapa nativa: sustituye a las rutinas del juego 0x98DA/0x9976 (capa 1) y 0x9C12/0x9CAE
| (capa 0), que copian bloques de 8x8 tiles del mapa a la VRAM del juego long a long.
| Entrada (como el original): a1 = desplazamiento en la capa, d3 = pasadas-1, d5 = longs por pasada-1.
| Cada long: valor ya descifrado (maptab) -> VRAM del juego + Neo Geo directo (.Vhave).
| Columnas completas (32 filas): solo VRAM del juego y luego la columna entera de golpe (col_refresh).
| Registros de trabajo: d6 contador interior, d7 coordenada que avanza, a4 fila del mapa,
| a5 destino, a6 origen (bit 31 = sin descifrar).
        .globl  map_col_l0, map_col_l1, map_row_l0, map_row_l1
        .macro  MROWPTR x, y
        moveq   #0,%d0
        move.w  \y,%d0
        lsr.w   #1,%d0
        andi.w  #0x1FC0,%d0
        move.w  \x,%d1
        lsr.w   #6,%d1
        andi.w  #0x3E,%d1
        or.w    %d1,%d0
        add.l   mc_rb,%d0
        movea.l %d0,%a4
        .endm
        .macro  MBLKPTR x, y
        move.w  \x,%d1
        lsr.w   #2,%d1
        andi.w  #0x1C,%d1
        move.w  \y,%d0
        add.w   %d0,%d0
        andi.w  #0xE0,%d0
        or.w    %d0,%d1
        moveq   #0,%d0
        move.w  (%a4),%d0
        lsl.l   #8,%d0
        or.b    %d1,%d0
        add.l   mc_bb,%d0
        bsr     .Mxlat
        .endm
| d0 = direccion del bloque en la ROM del juego -> a6 (traducida a maptab o con bit 31 si no)
.Mxlat: move.l  %d0,%d1
        lsr.l   #8,%d1
        lsr.l   #4,%d1
        cmpi.l  #255,%d1
        bcc.s   8f
        add.w   %d1,%d1
        lea     map_ptab,%a6
        adda.w  %d1,%a6
        move.w  (%a6),%d1
        bmi.s   8f
        | el bloque (256 bytes) puede cruzar a la pagina siguiente: debe seguirla en maptab
        move.w  %d0,%d2
        andi.w  #0xFFF,%d2
        cmpi.w  #0xF00,%d2
        bls.s   7f
        move.w  2(%a6),%d2
        sub.w   %d1,%d2
        subq.w  #1,%d2
        bne.s   8f
7:      andi.l  #0xFFFF,%d1
        lsl.l   #8,%d1
        lsl.l   #4,%d1
        andi.l  #0xFFF,%d0
        add.l   %d1,%d0
        lea     maptab,%a6
        adda.l  %d0,%a6
        rts
8:      ori.l   #0x80000000,%d0
        movea.l %d0,%a6
        rts
| un long: (a6) -> (a5) y VRAM Neo Geo
.Mput:  ori.w   #0x0700,%sr
        move.l  %a6,%d0
        bmi.s   1f
        move.w  (%a6),%d3
        move.w  2(%a6),%d1
        move.w  %d3,(%a5)
        move.w  %d1,2(%a5)
        move.l  %a5,%d0
        subi.l  #0x100000,%d0
        bsr     .Vhave
        move.w  mc_sr,%sr
        rts
1:      move.l  (%a6),(%a5)
        movea.l %a5,%a0
        move.l  %a5,%d0
        subi.l  #0x100000,%d0
        bsr     .Vcore
        move.w  mc_sr,%sr
        rts
| modo lote: solo la VRAM del juego
.Mputb: move.l  %a6,%d0
        bmi.s   .Mput
        move.l  (%a6),(%a5)
        rts

map_col_l0:
        move.l  #0x100000,%d0
        bra.s   1f
map_col_l1:
        move.l  #0x102000,%d0
1:      move.w  0x10C224,%d4
        andi.w  #0xF0,%d4
        cmpi.w  #0x90,%d4
        bne.s   2f
        rts
2:      movem.l %d2-%d7/%a2-%a6,-(%sp)
        move.w  %sr,mc_sr
        move.l  %d0,mc_base
        move.l  0x10C190,mc_rb
        move.l  0x10C188,mc_bb
        cmpi.l  #0x100000,%d0
        bne.s   9f
        move.l  0x10C194,mc_rb
        move.l  0x10C18C,mc_bb
9:
        movea.l %a1,%a5
        adda.l  %d0,%a5
        move.w  %d3,mc_cnt
        move.w  %d5,mc_n
        move.w  0x10C242,mc_fix         | x
        move.w  0x10C256,%d7            | y
        lea     .Mput,%a0
        cmpi.w  #11,%d5
        blt.s   3f
        lea     .Mputb,%a0
3:      move.l  %a0,mc_fn
.Ccol:  move.l  %a5,mc_dst0
        sf      mc_chg
        move.w  %d7,mc_mov0
        move.w  mc_n,%d6
        MROWPTR mc_fix, %d7
.CA:    MBLKPTR mc_fix, %d7
        cmpi.l  #.Mputb,mc_fn
        bne.s   .CB
        move.l  %a6,%d0
        bmi.s   .CB
        | lote con bloque descifrado: copia directa
.CBf:   move.l  (%a6),%d0
        cmp.l   (%a5),%d0
        beq.s   7f
        move.l  %d0,(%a5)
        st      mc_chg                  | (algo ha cambiado en la columna)
7:      lea     0x100(%a5),%a5
        dbf     %d6,1f
        bra.s   .Cdone
1:      lea     32(%a6),%a6
        addi.w  #16,%d7
        moveq   #0x70,%d0
        and.w   %d7,%d0
        bne.s   .CBf
        bra.s   .Cwrap
.CB:    movea.l mc_fn,%a0
        jsr     (%a0)
        lea     0x100(%a5),%a5
        dbf     %d6,1f
        bra.s   .Cdone
1:      lea     32(%a6),%a6
        addi.w  #16,%d7
        move.w  %d7,%d0
        andi.w  #0x70,%d0
        bne.s   .CB
.Cwrap: move.w  %d7,%d0
        andi.w  #0x180,%d0
        bne.s   2f
        suba.w  #0x2000,%a5
2:      lea     64(%a4),%a4
        move.w  %d7,%d0
        andi.w  #0x3F80,%d0
        bne     .CA
        MROWPTR mc_fix, %d7
        bra     .CA
.Cdone: cmpi.l  #.Mputb,mc_fn
        bne.s   4f
        move.l  %a6,%d0
        bmi.s   8f                      | (bloque cifrado: .Mput ya ha escrito)
        tst.b   mc_chg
        beq.s   4f                      | columna sin cambios: la copia Neo Geo ya esta bien
8:
        | columna completa: mascaras y sprites de golpe (col_write_asm corta las IRQ por tramos)
        move.l  mc_dst0,%d1
        sub.l   mc_base,%d1
        lsr.w   #2,%d1
        andi.l  #63,%d1
        moveq   #0,%d0
        cmpi.l  #0x100000,mc_base
        beq.s   5f
        moveq   #1,%d0
5:      lea     col_refresh,%a1
        bsr     onpriv
        move.w  mc_sr,%sr
4:      move.w  mc_mov0,%d7
        addi.w  #16,mc_fix
        movea.l mc_dst0,%a5
        addq.l  #4,%a5
        move.w  %a5,%d0
        andi.w  #0xFF,%d0
        bne.s   6f
        suba.w  #0x100,%a5
6:      subq.w  #1,mc_cnt
        cmpi.w  #-1,mc_cnt
        bne     .Ccol
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts

map_row_l0:
        move.l  #0x100000,%d0
        bra.s   1f
map_row_l1:
        move.l  #0x102000,%d0
1:      move.w  0x10C224,%d4
        andi.w  #0xF0,%d4
        cmpi.w  #0x90,%d4
        bne.s   2f
        rts
2:      .ifdef PROFB
        move.w  #3,0x10BFF6
        .endif
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        move.w  %sr,mc_sr
        move.l  %d0,mc_base
        move.l  0x10C190,mc_rb
        move.l  0x10C188,mc_bb
        move.w  #128,mc_lo
        move.b  #1,mc_l1
        cmpi.l  #0x100000,%d0
        bne.s   9f
        move.l  0x10C194,mc_rb
        move.l  0x10C18C,mc_bb
        move.w  #0,mc_lo
        move.b  #0,mc_l1
9:
        movea.l %a1,%a5
        adda.l  %d0,%a5
        move.w  %d3,mc_cnt
        move.w  %d5,mc_n
        move.w  0x10C256,mc_fix         | y
        move.w  0x10C242,%d7            | x
        move.l  %a5,mc_dst00
        lea     .Mput,%a0
        cmpi.w  #7,%d3
        blt.s   3f
        lea     .Mputb,%a0
3:      move.l  %a0,mc_fn
.Rrow:  move.l  %a5,mc_dst0
        move.w  %d7,mc_mov0
        move.w  mc_n,%d6
        MROWPTR %d7, mc_fix
.RA:    MBLKPTR %d7, mc_fix
        cmpi.l  #.Mputb,mc_fn
        beq.s   1f
        move.l  %a6,%d0
        .ifndef NORF
        bpl     .RF                     | bloque ya descifrado: camino rapido
        .endif
        bra.s   .RB
1:      move.l  %a6,%d0
        bmi.s   .RB
.RBf:   move.l  (%a6)+,(%a5)+
        dbf     %d6,1f
        bra     .Rdone
1:      addi.w  #16,%d7
        moveq   #0x70,%d0
        and.w   %d7,%d0
        bne.s   .RBf
        bra     .Rwrap
.RB:    movea.l mc_fn,%a0
        jsr     (%a0)
        addq.l  #4,%a5
        dbf     %d6,1f
        bra     .Rdone
1:      addq.l  #4,%a6
        addi.w  #16,%d7
        move.w  %d7,%d0
        andi.w  #0x70,%d0
        bne.s   .RB
| camino rapido de una fila (bloque descifrado, sin lote): VRAM del juego y, solo si la columna
| se ve, la de Neo Geo (mismo trabajo que .Mput/.Vhave sin repetir lo que no cambia en la fila)
.RF:    move.l  (%a6)+,%d4              | e0:e1
        cmp.l   (%a5),%d4               | igual que lo que ya hay: la copia Neo Geo ya lo tiene
        beq     .RFn
        move.l  %d4,(%a5)
        move.w  %a5,%d0
        lsr.w   #1,%d0
        andi.w  #126,%d0
        add.w   mc_lo,%d0               | (capa*64 + columna)*2
        lea     cb1,%a1
        tst.w   (%a1,%d0.w)
        beq     .RFn                    | columna sin ranura: nada mas
        movea.w %d0,%a3
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        move.w  #1,0x3C0004
        move.l  %d4,%d1                 | d1.b = atributo del juego
        swap    %d4
        andi.w  #0x3FFF,%d4             | tile
        moveq   #0,%d2
        move.b  %d1,%d2
        add.w   %d2,%d2
        lea     tattr_tab,%a1
        move.w  (%a1,%d2.w),%d5         | atributo
        move.w  %a5,%d2
        lsr.w   #7,%d2
        andi.w  #62,%d2
        movea.w %d2,%a0                 | fila*2
        move.w  %a5,%d3
        lsr.w   #2,%d3
        andi.w  #63,%d3                 | columna
        move.w  %a3,%d2
        tst.b   mc_l1
        bne.s   2f
        bsr     .Vl0                    | capa 0: mascaras, capa alta y VRAM
        bra.s   3f
2:      move.w  %d4,%d0                 | capa 1 (variante con el pen 0 opaco, si la hay)
        add.w   %d0,%d0
        lea     alt1_tab,%a1
        move.w  (%a1,%d0.w),%d0
        beq.s   4f
        move.w  %d0,%d4
        addi.w  #0x7000,%d5
4:      lea     cb1,%a1
        move.w  (%a1,%d2.w),%d0
        beq.s   3f
        add.w   %a0,%d0
        move.w  %d0,0x3C0000
        move.w  %d4,0x3C0002
        move.w  %d5,0x3C0002
        lea     cb2,%a1
        move.w  (%a1,%d2.w),%d0
        beq.s   3f
        add.w   %a0,%d0
        move.w  %d0,0x3C0000
        move.w  %d4,0x3C0002
        move.w  %d5,0x3C0002
3:      move.w  (%sp)+,%sr
.RFn:   addq.l  #4,%a5
        dbf     %d6,1f
        bra     .Rdone
1:      addi.w  #16,%d7
        moveq   #0x70,%d0
        and.w   %d7,%d0
        bne     .RF
.Rwrap: move.w  %d7,%d0
        andi.w  #0x380,%d0
        bne.s   2f
        suba.w  #0x100,%a5
2:      addq.l  #2,%a4
        move.w  %d7,%d0
        andi.w  #0xF80,%d0
        bne     .RA
        MROWPTR %d7, mc_fix
        bra     .RA
.Rdone: move.w  mc_mov0,%d7
        addi.w  #16,mc_fix
        movea.l mc_dst0,%a5
        lea     0x100(%a5),%a5
        move.w  %a5,%d0
        andi.w  #0x1F00,%d0
        bne.s   6f
        suba.w  #0x2000,%a5
6:      subq.w  #1,mc_cnt
        cmpi.w  #-1,mc_cnt
        bne     .Rrow
        cmpi.l  #.Mputb,mc_fn
        bne.s   9f
        | modo lote: refrescar las columnas tocadas (col_write_asm corta las IRQ por tramos)
        move.l  mc_dst00,%d7
        sub.l   mc_base,%d7
        lsr.w   #2,%d7
        move.w  mc_n,%d6
1:      moveq   #0,%d0
        cmpi.l  #0x100000,mc_base
        beq.s   5f
        moveq   #1,%d0
5:      moveq   #63,%d1
        and.w   %d7,%d1
        lea     col_refresh,%a1
        bsr     onpriv
        addq.w  #1,%d7
        dbf     %d6,1b
        move.w  mc_sr,%sr
9:      movem.l (%sp)+,%d2-%d7/%a2-%a6
        .ifdef PROFB
        move.w  #4,0x10BFF6
        .endif
        rts
        .bss
        .even
mc_sr:  .skip 2
mc_fix: .skip 2
mc_cnt: .skip 2
mc_n:   .skip 2
mc_mov0: .skip 2
mc_lo:  .skip 2
mc_l1:  .skip 1
mc_chg: .skip 1
        .even
mc_base: .skip 4
mc_dst0: .skip 4
mc_dst00: .skip 4
mc_fn:  .skip 4
mc_rb:  .skip 4
mc_bb:  .skip 4
        .text

| fin de cuadro del bucle principal (sustituye a "tst.w $FEC1A6 / beq" en 0x32B8): convierte
| los sprites del cuadro recien terminado y luego espera al VBLANK como el original
        .globl  frame_hook
frame_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        lea     frame_convert,%a1
        bsr     onpriv
        .ifdef PROFB
        move.w  #31,0x10BFF6
        .endif
        | mientras se espera al VBLANK: columnas que falten, en ranuras ocultas
1:      lea     q_service,%a1           | cuadro en cola sin convertir (VBLANK ocupado): ya
        bsr     onpriv
        tst.w   0x10C1A6
        bne.s   2f
        bsr     .Tsyn                   | adelantar el siguiente cuadro si se puede
        tst.w   %d0
        bne.s   2f
        moveq   #1,%d0
        lea     layer_fill_one,%a1
        bsr     onpriv
        tst.w   %d0
        bne.s   1b
        moveq   #0,%d0
        lea     layer_fill_one,%a1
        bsr     onpriv
        tst.w   %d0
        bne.s   1b
        lea     pal_idle_step,%a1       | paletas (una parte por vuelta)
        bsr     onpriv
        tst.w   %d0
        bne.s   1b
        lea     eff_idle_step,%a1       | paletas de efecto (un banco por vuelta)
        bsr     onpriv
        tst.w   %d0
        bne.s   1b
3:      lea     q_service,%a1
        bsr     onpriv
        tst.w   0x10C1A6
        bne.s   2f
        bsr     .Tsyn
        tst.w   %d0
        beq.s   3b
2:      .ifdef PROFB
        move.w  #32,0x10BFF6
        .endif
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

| tick adelantado: si el juego no va mas de LEADMAX ticks por delante de los VBLANK y caben mas
| cuadros por mostrar (el pendiente + uno en cola), se ejecuta ya la IRQ del juego (como haria
| el VBLANK) y el bucle principal sigue con el siguiente cuadro. Asi el tiempo libre de los
| cuadros ligeros adelanta cuadros y los pesados no dejan la pantalla sin cuadro nuevo.
| Devuelve d0 = 1 si lo ha hecho. (Tambien paga ticks atrasados: tick_lead < 0)
        .equ    LEADMAX, 2
.Tsyn:  moveq   #0,%d0
        .ifdef NOTSYN
        rts
        .endif
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        tst.w   0x10C1A6
        bne.s   9f
        tst.b   chain_busy
        bne.s   9f
        cmpi.b  #LEADMAX,tick_lead
        bge.s   9f
        move.b  spr_ready,%d1
        add.b   q_cnt,%d1
        cmpi.b  #3,%d1                  | (el pendiente + 2 en cola)
        bcc.s   9f
        addq.b  #1,tick_lead
        move.b  #1,chain_busy
        move.w  (%sp)+,%sr
        movem.l %d1-%d7/%a0-%a6,-(%sp)
        pea     8f(%pc)
        move.w  %sr,-(%sp)
        jmp     0x002F8C
8:      clr.b   chain_busy
        | si el VBLANK llego durante esta IRQ no pudo convertir el cuadro en cola: ahora
        lea     q_service,%a1
        bsr     onpriv
        movem.l (%sp)+,%d1-%d7/%a0-%a6
        moveq   #1,%d0
        rts
9:      move.w  (%sp)+,%sr
        rts

| sustituye a la rutina 0x44E6 del juego (copia de las listas de sprites a la RAM de sprites,
| dentro de su IRQ de VBLANK): convertimos las listas directamente a sprites Neo Geo
        .globl  spr_hook
spr_hook:
        | la conversion es larga: se hace con las interrupciones abiertas para que el siguiente
        | VBLANK llegue a su hora (vuelca el ultimo cuadro completo; buffers dobles)
        move.w  %sr,-(%sp)
        andi.w  #0xF8FF,%sr
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        lea     spr_update_c,%a1
        bsr     onpriv
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        move.w  (%sp)+,%sr
        rts

| escritura al OKI: el byte ya esta en 0x10A00F (instruccion original reubicada)
oki_hook:
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr             | atomico respecto a la IRQ (el driver de sonido del juego corre en ella)
        moveq   #0,%d0
        move.b  0x10A00F,%d0
        lea     oki_write,%a1
        bsr     onpriv
        move.w  (%sp)+,%sr
        rts
        .globl  oki_hook
| sustituye a "move.w #1,$fec102.l" (peticion a la MCU)
mcu_hook:
        move.w  %sr,-(%sp)
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        move.w  #1,0x10C102
        lea     mcu_task,%a1
        bsr     onpriv
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        move.w  (%sp)+,%ccr
        andi.b  #0xF0,%ccr              | flags como move.w #1 (N=Z=V=C=0)
        rts

| VBLANK Neo Geo (IRQ1) -> nuestro trabajo + manejador IRQ6 original (0x2F8C)
vblank_irq:
        cmpi.w  #0x5752,0x10BFFE
        beq.s   1f
        jmp     0xC00438                | SYSTEM_INT1 de la BIOS mientras arranca
1:      move.w  #4,0x3C000C
        move.b  %d0,0x300001
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        .ifdef PROFB
        move.w  #7,0x10BFF6
        .endif
        lea     ng_vblank,%a1
        bsr     onpriv
        .ifdef PROFB
        move.w  #8,0x10BFF6
        .endif
        subq.b  #1,tick_lead            | (un VBLANK mas: el juego tiene un tick menos de ventaja)
        cmpi.b  #-3,tick_lead
        bge.s   6f
        move.b  #-3,tick_lead           | (atraso maximo: si no, el juego se ralentiza)
6:      tst.b   chain_busy              | VBLANK anidado: solo nuestro trabajo
        beq.s   5f
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        rte
5:
        | cuadro en cola: se convierte ya, justo despues de mostrar el anterior, aunque luego se
        | salte la IRQ del juego (con las interrupciones abiertas; un VBLANK anidado solo hace lo nuestro)
        move.b  #1,chain_busy
        move.w  %sr,-(%sp)
        andi.w  #0xF8FF,%sr
        lea     q_service,%a1
        bsr     onpriv
        move.w  (%sp)+,%sr
        clr.b   chain_busy
        | ritmo del juego: cada cuadro del bucle principal va precedido de un tick (la IRQ del
        | juego). tick_lead = ticks hechos - VBLANKs: si el juego va por detras se hace aqui; si va
        | por delante (ha adelantado cuadros en tiempo libre, ver frame_hook) no hace falta.
        | Si el bucle no ha recogido el tick anterior (va retrasado) no se le da otro, salvo una de
        | cada dos veces (pantallas cuyo bucle no lo recoge)
        tst.w   0x10C1A6
        beq.s   3f
        tst.b   irq_skipped
        bne.s   2f
        move.b  #1,irq_skipped
        .ifdef DBGSPD
        addq.w  #1,dbg_skip
        .endif
        bra.s   7f
3:      tst.b   tick_lead
        bpl.s   7f                      | va por delante: nada
2:      clr.b   irq_skipped
        addq.b  #1,tick_lead
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        | la IRQ del juego se llama con un marco propio: su RTE vuelve aqui y asi sabemos cuando
        | termina. Si llega otro VBLANK mientras tanto (durante la conversion de sprites, que va con
        | las interrupciones abiertas) no se vuelve a entrar en ella
        move.b  #1,chain_busy
        .ifdef PROFB
        move.w  #5,0x10BFF6
        .endif
        pea     4f(%pc)
        move.w  %sr,-(%sp)
        jmp     0x002F8C
4:      clr.b   chain_busy
        .ifdef PROFB
        move.w  #6,0x10BFF6
        .endif
        rte
7:      movem.l (%sp)+,%d0-%d1/%a0-%a1
        rte
        .bss
        .globl  tick_lead
tick_lead: .skip 1
        .ifdef DBGSPD
        .even
        .globl  dbg_skip
dbg_skip: .skip 2
        .endif
irq_skipped: .skip 1
vchg:   .skip 1
        .globl  chain_busy
chain_busy: .skip 1
        .text
        .ifdef DIAG3
| diagnostico: lecturas de vuelta de la VRAM del juego al empezar una etapa
        .text
        .globl  dbg_h1, dbg_h2
dbg_h1: move.l  0x1017b4,%d0            | (instruccion original de 0x907a)
        move.l  %d0,dbg_v0
        move.w  0x10F378,dbg_v1
        move.w  0x10F37A,dbg_v2
        move.w  0x10C19E,dbg_v3
        addq.w  #1,dbg_n1
        tst.l   %d0
        rts
dbg_h2: move.l  0x100008,%d3            | (instruccion original de 0x3e10)
        move.l  %d3,dbg_v4
        move.w  0x10C198,dbg_v5
        addq.w  #1,dbg_n2
        tst.l   %d3
        rts
        .bss
        .even
        .globl  dbg_v0, dbg_v1, dbg_v2, dbg_v3, dbg_v4, dbg_v5, dbg_n1, dbg_n2
dbg_v0: .skip 4
dbg_v4: .skip 4
dbg_v1: .skip 2
dbg_v2: .skip 2
dbg_v3: .skip 2
dbg_v5: .skip 2
dbg_n1: .skip 2
dbg_n2: .skip 2
        .text
        .endif
| inicio de etapa (sustituye a "clr.w $fef518" en 0x9168): ademas, coche parado. El original no
| pone a cero la velocidad (0xFEEAA8/0xFEEAAC) y el juego la sigue aplicando durante el "Ready":
| si la carrera anterior acabo con el coche moviendose (perder y continuar, rally nuevo), el coche
| salia desplazado de la linea de salida (fuera de la carretera)
        .text
        .globl  stage_init_fix
stage_init_fix:
        clr.w   0x10F518
        clr.l   0x10EAA8
        clr.l   0x10EAAC
        | y se mantiene el coche en la salida hasta que empieza a correr el tiempo (main.c:
        | start_hold); no en las demos (0xFEC172 bit 0: posicion propia)
        btst    #0,0x10C173
        bne.s   1f
        move.w  0x10EA8C,start_a
        move.w  0x10EA90,start_b
        move.l  0x10C1AA,start_t0
        st      start_arm
1:
        .ifdef DIAG4
        bsr     d4_reset
        .endif
        rts
        .bss
        .even
        .globl  start_a, start_b, start_t0, start_arm
start_t0: .skip 4
start_a: .skip 2
start_b: .skip 2
start_arm: .skip 1
        .even
        .text
        .ifdef DIAG4
| diagnostico 4: quien escribe la posicion (0xFEEAA0/A4) y la velocidad (0xFEEAA8/AC) del coche
        .text
        .macro  DREC id
        move.w  %sr,-(%sp)
        addq.b  #1,d4_cnt+\id
        move.b  #\id,d4_last
        move.w  0x10EAA0,d4_la
        move.w  0x10EAA4,d4_lb
        move.w  0x10EAA8,d4_lv
        move.w  ng_frames,d4_lf
        move.w  (%sp)+,%ccr
        .endm
        .globl  d4_h1, d4_h2, d4_h3, d4_h4, d4_h5, d4_h6, d4_h7, d4_h8, d4_h9
d4_h1:  move.l  0x10EA8C,0x10EAA0
        DREC 1
        rts
d4_h2:  move.w  0x10C176,0x10EAA0
        DREC 2
        rts
d4_h3:  move.w  0x10EB1C,0x10EAA0
        DREC 3
        rts
d4_h4:  move.l  0x10202C,0x10EAA0
        DREC 4
        rts
d4_h5:  add.l   %d0,0x10EAA0
        DREC 5
        rts
d4_h6:  move.l  %d0,0x10EAA0
        DREC 6
        rts
d4_h7:  move.l  %d0,0x10EAA8
        DREC 7
        rts
d4_h8:  add.l   %d1,0x10EAA8
        DREC 8
        rts
d4_h9:  add.l   %d1,0x10EAA8
        DREC 9
        rts
        .globl  d4_reset
d4_reset:
        clr.l   d4_cnt
        clr.l   d4_cnt+4
        clr.w   d4_cnt+8
        clr.b   d4_last
        rts
        .bss
        .even
        .globl  d4_cnt, d4_last, d4_la, d4_lb, d4_lv, d4_lf
d4_cnt: .skip 10
d4_la:  .skip 2
d4_lb:  .skip 2
d4_lv:  .skip 2
d4_lf:  .skip 2
d4_last: .skip 1
        .even
        .text
        .endif
