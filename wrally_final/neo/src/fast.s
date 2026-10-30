| Rutinas criticas en ensamblador
        .text
| Paletas: NADA toca la RAM de paletas del Neo Geo fuera del VBLANK. En el hardware real (y en el
| nucleo de MiSTer) cada acceso de la CPU a esa RAM con la imagen activa hace que el pixel que se
| esta dibujando tome el color de la direccion accedida (puntos y rayas); MAME no lo emula.
| Las paletas convertidas se preparan en RAM (pal_stg, en las colas sin usar de los bancos 2-4 de
| paletas del juego: 16 paletas por banco) y se marcan en pal_dirty; en el VBLANK pal_copy_dirty
| copia solo las marcadas.
        .equ    PSTG0, 0x105600         | paletas 0-15 (banco 2, paletas 48-63 que el juego no usa)
        .equ    PSTG_STEP, 0x800        | 16-31 en 0x105E00, 32-47 en 0x106600
| u16 pal_update_part(u16 part): prepara 8 de las 48 paletas (part = 0..5) si han cambiado;
| devuelve cuantas
        .globl  pal_update_part, pal_stg_addr, pal_mark
pal_update_part:
        movem.l %d2-%d6/%a2-%a6,-(%sp)
        move.l  44(%sp),%d5
        lsl.w   #3,%d5                  | primera paleta
        move.b  pal_cvp,%d6             | (si interrumpe a otra conversion, se deja como estaba)
        move.w  %d5,%d0
        mulu.w  #32,%d0
        lea     0x104000,%a0
        adda.l  %d0,%a0
        lea     pal_cache,%a3
        adda.l  %d0,%a3
        lea     col_tab,%a2
        lea     pstg_tab,%a6
        move.w  #0x0FFF,%d2
        moveq   #7,%d3
        moveq   #0,%d1                  | paletas cambiadas (valor devuelto)
.Lpal:  movea.l %a0,%a4
        movea.l %a3,%a5
        moveq   #7,%d4
.Lcmp:  cmpm.l  (%a4)+,(%a5)+
        dbne    %d4,.Lcmp
        beq.s   .Lsame
        move.b  %d5,pal_cvp             | (el VBLANK no copia esta mientras se escribe)
        move.w  %d5,%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        movea.l (%a6,%d0.w),%a1
        movea.l %a0,%a4
        movea.l %a3,%a5
        moveq   #15,%d4
.Lcv:   move.w  (%a4)+,%d0
        move.w  %d0,(%a5)+
        and.w   %d2,%d0
        add.w   %d0,%d0
        move.w  (%a2,%d0.w),(%a1)+
        dbra    %d4,.Lcv
        move.w  %d5,%d0
        bsr.s   pal_mark_d0
        move.b  %d6,pal_cvp
        addq.w  #1,%d1
.Lsame: lea     32(%a0),%a0
        lea     32(%a3),%a3
        addq.w  #1,%d5
        dbra    %d3,.Lpal
        move.l  %d1,%d0
        movem.l (%sp)+,%d2-%d6/%a2-%a6
        rts
| marca la paleta d0.w (0..47) para copiarla en el VBLANK: cola de indices sin repetir
| (con las interrupciones cortadas: la marca tambien se hace desde el VBLANK). Conserva d1
pal_mark_d0:
        move.l  %a0,-(%sp)
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        lea     pal_dirty,%a0
        tst.b   (%a0,%d0.w)
        bne.s   1f
        move.b  #1,(%a0,%d0.w)
        move.l  %d1,-(%sp)
        moveq   #0,%d1
        move.b  pal_qt,%d1
        lea     pal_q,%a0
        move.b  %d0,(%a0,%d1.w)
        addq.b  #1,%d1
        andi.b  #63,%d1
        move.b  %d1,pal_qt
        move.l  (%sp)+,%d1
1:      move.w  (%sp)+,%sr
        movea.l (%sp)+,%a0
        rts
| void pal_mark(u16 p)
pal_mark:
        move.l  4(%sp),%d0
        bra.s   pal_mark_d0
| u16 *pal_stg_addr(u16 p): direccion de la paleta p preparada
pal_stg_addr:
        move.l  4(%sp),%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        lea     pstg_tab,%a0
        movea.l (%a0,%d0.w),%a0
        move.l  %a0,%d0                 | (gcc m68k devuelve los punteros en a0)
        rts
pstg_tab:
        .irp    b,0,1,2
        .irp    i,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        .long   PSTG0 + \b * PSTG_STEP + \i * 32
        .endr
        .endr
| void pal_copy_dirty(void): (VBLANK) copia a las paletas Neo Geo 16-63 las marcadas, en orden,
| hasta la que el bucle principal este escribiendo (pal_cvp), si la hay
        .globl  pal_copy_dirty
pal_copy_dirty:
        move.b  pal_qh,%d0
        cmp.b   pal_qt,%d0
        bne.s   0f
        rts
0:      movem.l %d2-%d7/%a2-%a4,-(%sp)
        moveq   #0,%d1
        move.b  pal_qh,%d1
        lea     pal_q,%a2
        lea     pal_dirty,%a3
1:      cmp.b   pal_qt,%d1
        beq.s   9f
        moveq   #0,%d0
        move.b  (%a2,%d1.w),%d0
        cmp.b   pal_cvp,%d0
        beq.s   9f                      | (a medio escribir: en el VBLANK siguiente)
        move.b  #0,(%a3,%d0.w)
        add.w   %d0,%d0
        add.w   %d0,%d0
        lea     pstg_tab,%a0
        movea.l (%a0,%d0.w),%a0
        lsl.w   #3,%d0                  | p * 32
        movea.l #0x400200,%a1
        adda.w  %d0,%a1
        movem.l (%a0),%d0/%d2-%d7/%a4
        movem.l %d0/%d2-%d7/%a4,(%a1)
        | paletas de la capa 1 (juego 0-31): tambien la gemela (Neo 128+p) con el pen 15 = color del
        | pen 0, para las variantes de tile con el pen 0 opaco
        cmpa.l  #0x400200+32*32,%a1
        bcc.s   2f
        lea     0xE00(%a1),%a1
        movem.l %d0/%d2-%d7,(%a1)
        move.l  %a4,%d2
        swap    %d0
        move.w  %d0,%d2
        move.l  %d2,28(%a1)
2:      addq.b  #1,%d1
        andi.b  #63,%d1
        bra.s   1b
9:      move.b  %d1,pal_qh
        movem.l (%sp)+,%d2-%d7/%a2-%a4
        rts
        | marcas y cola en la cola sin usar del banco 5 de paletas del juego (sin RAM libre; se
        | ponen a cero en video_init)
        .equ    pal_dirty, 0x107600     | 48 bytes
        .equ    pal_q, 0x107640         | 64 bytes
        .bss
        .globl  pal_cvp, pal_qh, pal_qt
pal_cvp:   .skip 1
pal_qh:    .skip 1
pal_qt:    .skip 1
        .even
        .globl  pal_cache
pal_cache: .skip 1536
        .text

| void mcu_task_rc(mcs51_t *c): ejecuta la tarea del 8051 recompilada (0x0171 -> 0x018C)
        .globl  mcu_task_rc
mcu_task_rc:
        movem.l %d2-%d7/%a2-%a5,-(%sp)
        movea.l 44(%sp),%a0
        movea.l %a0,%a5
        lea     256(%a0),%a4
        move.b  0x160(%a0),%d7
        tst.b   0x150(%a0)
        smi     %d6
        moveq   #0,%d5
        move.b  0x103(%a0),%d5
        lsl.w   #8,%d5
        move.b  0x102(%a0),%d5
        lea     mcu_rom,%a2
        lea     0x10C000,%a1
        move.l  %a0,-(%sp)
        jsr     mcu_rc_entry
        movea.l (%sp)+,%a0
        move.b  %d7,0x160(%a0)
        andi.b  #0x7F,0x150(%a0)
        tst.b   %d6
        beq.s   1f
        ori.b   #0x80,0x150(%a0)
1:      move.b  %d5,0x102(%a0)
        lsr.w   #8,%d5
        move.b  %d5,0x103(%a0)
        movem.l (%sp)+,%d2-%d7/%a2-%a5
        rts

| conversion de listas de sprites del juego (por tramos), a buffers en RAM:
|   spr_begin(); spr_add(lista, n) ...; spr_end() -> (n0 << 16) | n1  (sprites Neo Geo por clase)
| Los tiles consecutivos de la lista que forman una columna (misma x, y + 16, misma clase) se juntan
| en un solo sprite Neo Geo de varios tiles: menos sprites por linea (el limite del hardware es 96).
| Registro: SCB3 (y | altura), SCB4, y (tile, atributo) por cada tile.
        .globl  spr_begin, spr_add, spr_end, spr_break, rec0, rec1
        .globl  sc_p0, sc_p1, sc_n0, sc_n1, sc_l0b, sc_l1b
        .equ    REC0SZ, 800
        .equ    REC1SZ, 1656
spr_begin:
        movea.l 4(%sp),%a0              | buffer de la clase 0
        move.l  %a0,sc_p0
        lea     REC0SZ-4(%a0),%a1
        move.l  %a1,sc_l0a
        subq.l  #4,%a1
        move.l  %a1,sc_l0b
        movea.l 8(%sp),%a0              | buffer de la clase 1
        move.l  %a0,sc_p1
        lea     REC1SZ-4(%a0),%a1
        move.l  %a1,sc_l1a
        subq.l  #4,%a1
        move.l  %a1,sc_l1b
        clr.l   sc_h0
        clr.l   sc_h1
        clr.w   sc_n0
        clr.w   sc_n1
        moveq   #-1,%d0
        move.l  %d0,sc_e0
        move.l  %d0,sc_e1
        rts
| u16 lcmp(const void *a, const void *b, u32 nl): 1 si los nl longs son iguales
        .globl  lcmp
lcmp:   movea.l 4(%sp),%a0
        movea.l 8(%sp),%a1
        move.l  12(%sp),%d1
        moveq   #1,%d0
        subq.w  #1,%d1
        bcs.s   2f
1:      cmpm.l  (%a0)+,(%a1)+
        dbne    %d1,1b
        beq.s   2f
        moveq   #0,%d0
2:      rts
| cierra los registros abiertos y no deja unir el siguiente tile con ellos (corte entre listas)
spr_break:
        bsr     spr_end
        moveq   #-1,%d0
        move.l  %d0,sc_e0
        move.l  %d0,sc_e1
        rts
| cierra el registro abierto de cada clase (altura en la cabecera)
spr_end:
        movea.l sc_h0,%a0
        move.l  %a0,%d0
        beq.s   1f
        move.w  sc_e0+2,%d0
        andi.w  #0x7F,%d0
        subi.w  #32,%d0
        or.w    %d0,(%a0)
1:      movea.l sc_h1,%a0
        move.l  %a0,%d0
        beq.s   2f
        move.w  sc_e1+2,%d0
        andi.w  #0x7F,%d0
        subi.w  #32,%d0
        or.w    %d0,(%a0)
2:      clr.l   sc_h0
        clr.l   sc_h1
        move.w  sc_n0,%d0
        swap    %d0
        move.w  sc_n1,%d0
        rts
| clave de union por clase (d5 / d6): x (SCB4) en la parte alta; en la baja, y esperada del
| siguiente tile (bits 7-15) y altura + 32 (bits 0-6: con 32 tiles se activa el bit 6 y ya no casa)
spr_add:
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        movea.l 48(%sp),%a0
        move.l  52(%sp),%d7
        movea.l sc_p0,%a3
        movea.l sc_p1,%a4
        move.l  sc_e0,%d5
        move.l  sc_e1,%d6
        lea     spr_xtab,%a1
        lea     spr_ytab,%a2
        lea     spr_atab,%a6
        subq.w  #1,%d7
        bcs     .Cend
.Cl:    clr.l   sc_mt
        move.w  (%a0),%d0
        moveq   #0,%d1
        move.b  %d0,%d1
        add.w   %d1,%d1
        move.w  (%a2,%d1.w),%d2         | SCB3 (0 = fuera)
        beq     .Cnx
        move.w  4(%a0),%d3
        move.w  %d3,%d1
        andi.w  #0x3FF,%d1
        add.w   %d1,%d1
        move.w  (%a1,%d1.w),%d4         | SCB4 (bit0 = fuera)
        btst    #0,%d4
        bne     .Cnx
        rol.w   #6,%d3
        andi.w  #0x1F,%d3
        add.w   %d3,%d3
        add.w   %d3,%d3
        move.l  (%a6,%d3.w),%d3
        rol.w   #2,%d0
        andi.w  #3,%d0
        or.w    %d0,%d3                 | d3.w = atributo, parte alta = desplazamiento de tile
        andi.w  #0xFF80,%d2             | y sin altura
        move.w  %d4,%d0
        swap    %d0
        move.w  %d2,%d0                 | clave del tile
        move.w  6(%a0),%d1
        andi.w  #0x3FFF,%d1
        cmpi.w  #0x3700,%d1
        bcc     .Cc1
        swap    %d3
        add.w   %d3,%d1                 | tile final
        swap    %d3
        | ---- clase 0 ----
        move.l  %d5,%d4
        andi.w  #0xFFC0,%d4
        cmp.l   %d4,%d0
        bne.s   1f
        cmpa.l  sc_l0a,%a3
        bcc     .Cov0
        move.l  %a3,sc_mt
        move.w  %d1,(%a3)+
        move.w  %d3,(%a3)+
        subi.w  #0x7FF,%d5
        bra     .Cnx
1:      move.w  sc_n0,%d4
        cmp.w   vconst+12,%d4
        .ifdef CAPCNT
        bcs.s   91f
        addq.w  #1,0x10BFE0
        bra     .Cnx
91:
        .else
        bcc     .Cov0
        .endif
        cmpa.l  sc_l0b,%a3
        .ifdef CAPCNT
        bcs.s   92f
        addq.w  #1,0x10BFE2
        bra     .Cnx
92:
        .else
        bcc     .Cov0
        .endif
        movea.l sc_h0,%a5               | cerrar el anterior
        move.l  %a5,%d4
        beq.s   2f
        move.w  %d5,%d4
        andi.w  #0x7F,%d4
        subi.w  #32,%d4
        or.w    %d4,(%a5)
2:      move.l  %a3,sc_h0
        move.w  %d2,(%a3)+
        swap    %d0
        move.w  %d0,(%a3)+
        swap    %d0
        move.l  %a3,sc_mt
        move.w  %d1,(%a3)+
        move.w  %d3,(%a3)+
        move.l  %d0,%d5
        subi.w  #0x7FF-32,%d5           | y - 0x800, altura 1 (+32)
        addq.w  #1,sc_n0
        bra     .Cnx
        | ---- clase 1 ----
.Cc1:   swap    %d3
        add.w   %d3,%d1
        swap    %d3
        bra.s   .Cc1t
        | clase 0 llena (sprites o buffer): el tile va con los de clase 1, por encima. En las
        | carreras no pasa (como mucho 45 de 48); en el final, con todo el publico en pantalla, el
        | coche dando vueltas no cabia y solo se veia su sombra
.Cov0:  moveq   #-1,%d5                 | (no unir mas tiles al registro de clase 0)
.Cc1t:  move.l  %d6,%d4
        andi.w  #0xFFC0,%d4
        cmp.l   %d4,%d0
        bne.s   1f
        cmpa.l  sc_l1a,%a4
        bcc     .Cnx
        move.l  %a4,sc_mt
        move.w  %d1,(%a4)+
        move.w  %d3,(%a4)+
        subi.w  #0x7FF,%d6
        bra     .Cnx
1:      move.w  sc_n1,%d4
        cmp.w   vconst+14,%d4
        .ifdef CAPCNT
        bcs.s   95f
        addq.w  #1,0x10BFE8
        bra     .Cnx
95:
        .else
        bcc     .Cnx
        .endif
        cmpa.l  sc_l1b,%a4
        .ifdef CAPCNT
        bcs.s   98f
        addq.w  #1,0x10BFEE
        bra     .Cnx
98:
        .else
        bcc     .Cnx
        .endif
        movea.l sc_h1,%a5
        move.l  %a5,%d4
        beq.s   2f
        move.w  %d6,%d4
        andi.w  #0x7F,%d4
        subi.w  #32,%d4
        or.w    %d4,(%a5)
2:      move.l  %a4,sc_h1
        move.w  %d2,(%a4)+
        swap    %d0
        move.w  %d0,(%a4)+
        swap    %d0
        move.l  %a4,sc_mt
        move.w  %d1,(%a4)+
        move.w  %d3,(%a4)+
        move.l  %d0,%d6
        subi.w  #0x7FF-32,%d6
        addq.w  #1,sc_n1
.Cnx:   tst.l   sc_map                  | (marcador: donde quedo el tile de cada entrada)
        beq.s   1f
        movea.l sc_map,%a5
        move.w  sc_mt+2,(%a5)+          | (parte baja de la direccion; 0 = ninguna)
        move.l  %a5,sc_map
1:      addq.l  #8,%a0
        dbra    %d7,.Cl
.Cend:  move.l  %a3,sc_p0
        move.l  %a4,sc_p1
        move.l  %d5,sc_e0
        move.l  %d6,sc_e1
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts

| void spr_back(u32 n, u32 slot, const u16 *rec): tiles (SCB1) y x (SCB4) de n registros en las
| ranuras slot.. del juego de sprites oculto (altura 0). Se llama fuera del VBLANK: cada registro
| con las interrupciones cortadas (el VBLANK tambien usa VRAMADDR)
        .globl  spr_back, spr_show, spr_hide
spr_back:
        movem.l %d2-%d5/%a2,-(%sp)
        move.l  24(%sp),%d1             | n
        move.l  28(%sp),%d3             | slot
        movea.l 32(%sp),%a2
        lea     0x3C0002,%a1
        move.w  %d3,%d0
        lsl.w   #6,%d0                  | SCB1
        move.w  %d3,%d2
        addi.w  #0x8400,%d2             | SCB4
        move.w  %d1,%d4
        subq.w  #1,%d4
        bcs.s   9f
1:      move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        move.w  #1,2(%a1)
        move.w  %d0,-2(%a1)
        move.w  (%a2),%d5
        andi.w  #63,%d5
        movea.w 2(%a2),%a0              | SCB4 de este registro
        addq.l  #4,%a2
        subq.w  #1,%d5
        bcs.s   3f
2:      move.w  (%a2)+,(%a1)
        nop                             | la VRAM necesita > 12 ciclos entre escrituras
        move.w  (%a2)+,(%a1)
        dbra    %d5,2b
3:      nop
        move.w  %d2,-2(%a1)
        move.w  %a0,(%a1)
        move.w  (%sp)+,%sr
        addi.w  #64,%d0
        addq.w  #1,%d2
        dbra    %d4,1b
9:      movem.l (%sp)+,%d2-%d5/%a2
        rts
| void spr_show(u32 n, u32 slot, const u16 *rec): SCB3 (y | altura) de n registros (en el VBLANK)
spr_show:
        movem.l %d2-%d5/%a2,-(%sp)
        move.l  24(%sp),%d1
        move.l  28(%sp),%d3
        movea.l 32(%sp),%a2
        lea     0x3C0002,%a1
        move.w  #1,2(%a1)
        move.w  %d3,%d0
        addi.w  #0x8200,%d0
        move.w  %d0,-2(%a1)
        move.w  %d1,%d4
        subq.w  #1,%d4
        bcs.s   9f
4:      move.w  (%a2),%d5
        move.w  %d5,(%a1)
        andi.w  #63,%d5
        add.w   %d5,%d5
        add.w   %d5,%d5
        addq.w  #4,%d5
        adda.w  %d5,%a2
        dbra    %d4,4b
9:      movem.l (%sp)+,%d2-%d5/%a2
        rts
| void spr_hide(u32 n, u32 slot): altura 0 (ocultos) para n sprites desde slot
spr_hide:
        move.l  4(%sp),%d1
        move.l  8(%sp),%d0
        lea     0x3C0002,%a1
        move.w  #1,2(%a1)
        addi.w  #0x8200,%d0
        move.w  %d0,-2(%a1)
        subq.w  #1,%d1
        bcs.s   9f
1:      move.w  #0,(%a1)                | nunca clr en registros de VRAM: el 68000 lee antes de escribir
        dbra    %d1,1b
9:      rts
        .bss
        .even
rec0:   .skip   REC0SZ
rec1:   .skip   REC1SZ
sc_p0:  .skip 4
sc_l0a: .skip 4
sc_l0b: .skip 4
        .even
        .globl  eff_np, eff_npl, eff_px, eff_py
eff_np: .skip 2
eff_npl: .skip 2
eff_px: .skip 16
eff_py: .skip 16
sc_l1a: .skip 4
sc_l1b: .skip 4
sc_p1:  .skip 4
sc_h0:  .skip 4
sc_h1:  .skip 4
sc_n0:  .skip 2
        .globl  sc_map
sc_map: .skip 4
sc_mt:  .skip 4
sc_n1:  .skip 2
        .even
sc_e0:  .skip 4
sc_e1:  .skip 4
        .text

| void col_write_asm(const u16 *src, u32 vaddr, u32 hiaddr, u32 *l0, u32 *c1)
| columna de 32 tiles a la VRAM (SCB1); si hiaddr != 0 (capa 0) tambien la variante alta y las
| mascaras de la columna (l0: filas con tile no vacio, c1: filas con categoria 1).
| Por tramos de 8 tiles con las interrupciones cortadas (el VBLANK tambien usa VRAMADDR):
| asi nunca se retrasa mucho
        .globl  col_write_asm
col_write_asm:
        movem.l %d2-%d7/%a2-%a4,-(%sp)
        movea.l 40(%sp),%a0             | origen
        lea     tattr_tab,%a1
        lea     0x3C0002,%a2
        lea     tile_empty_tab,%a4
        moveq   #0,%d5                  | mascara l0
        moveq   #0,%d6                  | mascara c1
        move.w  46(%sp),%d7             | direccion SCB1
        moveq   #3,%d4                  | 4 tramos
        suba.l  %a3,%a3                 | capa 0: sin variantes
        tst.w   50(%sp)
        bne.s   0f
        lea     alt1_tab,%a3            | capa 1: tiles con el pen 0 opaco (variante + paleta gemela)
0:
1:      move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        move.w  #1,2(%a2)
        move.w  %d7,-2(%a2)
        moveq   #7,%d3
2:      move.w  (%a0),%d0
        andi.w  #0x3FFF,%d0
        move.l  %a3,%d1
        beq.s   21f
        move.w  %d0,%d1
        add.w   %d1,%d1
        move.w  (%a3,%d1.w),%d1
        beq.s   21f
        move.w  %d1,(%a2)               | variante
        moveq   #0,%d1                  | (la VRAM necesita > 12 ciclos entre escrituras:
        move.b  3(%a0),%d1              |  el atributo se calcula entre las dos)
        add.w   %d1,%d1
        move.w  (%a1,%d1.w),%d2
        addi.w  #0x7000,%d2             | paleta gemela (16+p -> 128+p)
        move.w  %d2,(%a2)
        bra.s   22f
21:     move.w  %d0,(%a2)
        moveq   #0,%d1
        move.b  3(%a0),%d1
        add.w   %d1,%d1
        move.w  (%a1,%d1.w),(%a2)
22:     lsr.l   #1,%d5
        move.w  %d0,%d1
        lsr.w   #3,%d1
        move.b  (%a4,%d1.w),%d1
        andi.w  #7,%d0
        btst    %d0,%d1
        bne.s   3f
        bset    #31,%d5
3:      lsr.l   #1,%d6
        btst    #5,3(%a0)
        beq.s   4f
        bset    #31,%d6
4:      lea     256(%a0),%a0
        dbra    %d3,2b
        move.w  (%sp)+,%sr
        addi.w  #16,%d7
        dbra    %d4,1b
        tst.w   50(%sp)
        beq     9f
        movea.l 52(%sp),%a3
        move.l  %d5,(%a3)
        movea.l 56(%sp),%a3
        move.l  %d6,(%a3)
        | variante alta (pens 8-15) o tile vacio. Si la columna nueva no tiene tiles de categoria 1
        | y la ranura alta ya esta vacia (bit 31 de hiaddr), no hay nada que escribir
        tst.l   %d6
        bne.s   10f
        tst.b   48(%sp)
        bmi     9f
10:     movea.l 40(%sp),%a0
        move.w  50(%sp),%d7
        move.w  vconst+6,%d2            | tile vacio
        moveq   #3,%d4
5:      move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        move.w  #1,2(%a2)
        move.w  %d7,-2(%a2)
        moveq   #7,%d3
6:      btst    #5,3(%a0)
        beq.s   7f
        move.w  (%a0),%d0
        andi.w  #0x3FFF,%d0
        addi.w  #0x4000,%d0
        move.w  %d0,(%a2)
        moveq   #0,%d1
        move.b  3(%a0),%d1
        add.w   %d1,%d1
        move.w  (%a1,%d1.w),(%a2)
        bra.s   8f
7:      move.w  %d2,(%a2)
        nop
        move.w  #0,(%a2)
8:      lea     256(%a0),%a0
        dbra    %d3,6b
        move.w  (%sp)+,%sr
        addi.w  #16,%d7
        dbra    %d4,5b
9:      movem.l (%sp)+,%d2-%d7/%a2-%a4
        rts

| void xburst(u32 vaddr, u32 xb, u32 s0, u32 n, u32 tw): SCB4 de n ranuras seguidas;
| la ranura s0 esta en x = xb y cada ranura siguiente (circular) tw pixeles a la derecha
        .globl  xburst, hiburst
xburst:
        movem.l %d2-%d4,-(%sp)
        lea     0x3C0002,%a1
        move.w  #1,2(%a1)
        move.w  18(%sp),-2(%a1)
        move.w  22(%sp),%d0             | xb
        move.w  26(%sp),%d1             | s0
        move.w  30(%sp),%d3             | n
        move.w  34(%sp),%d2             | tw
        lsl.w   #7,%d0
        lsl.w   #7,%d2
        move.w  %d3,%d4
        sub.w   %d1,%d4
        mulu.w  %d2,%d4
        add.w   %d0,%d4                 | x (<<7) de la ranura 0
        move.w  %d1,%d3
        subq.w  #1,%d3
        bcs.s   2f
1:      move.w  %d4,(%a1)
        add.w   %d2,%d4
        dbra    %d3,1b
2:      move.w  30(%sp),%d3
        sub.w   %d1,%d3
        subq.w  #1,%d3
        bcs.s   4f
3:      move.w  %d0,(%a1)
        add.w   %d2,%d0
        dbra    %d3,3b
4:      movem.l (%sp)+,%d2-%d4
        rts
| void hiburst(u32 yv, u32 hide, u32 vaddr, u32 r0): SCB3 de la capa alta (yv si la columna tiene tiles altos)
hiburst:
        | void hiburst(yv, hide, vaddr, win, mask, c0, tab): SCB3 de NSLOT ranuras; la ranura s
        | muestra la columna tab[s] si esta en pantalla ((col - c0) mod 64 < NCOL) y, con mascara,
        | si tiene tiles en las filas visibles (mask[col] & win). mask = 0: sin mascara
        movem.l %d2-%d6/%a2,-(%sp)
        lea     0x3C0002,%a1
        move.w  #1,2(%a1)
        move.w  38(%sp),-2(%a1)
        move.l  40(%sp),%d4             | ventana de filas visibles (bits)
        move.w  30(%sp),%d2
        tst.w   34(%sp)
        beq.s   1f
        moveq   #0,%d2
1:      movea.l 52(%sp),%a0             | tabla de columnas por ranura
        movea.l 44(%sp),%a2             | mascara por columna (o 0)
        move.w  50(%sp),%d5             | c0
        move.w  vconst+8,%d6            | NCOL
        move.w  vconst+16,%d3           | NSLOT ranuras
        subq.w  #1,%d3
        move.l  %a2,%d0
        beq.s   5f
2:      moveq   #0,%d0
        move.b  (%a0)+,%d0
        cmpi.b  #0xFF,%d0
        beq.s   3f
        move.w  %d0,%d1
        sub.w   %d5,%d1
        andi.w  #63,%d1
        cmp.w   %d6,%d1
        bcc.s   3f
        add.w   %d0,%d0
        add.w   %d0,%d0
        move.l  (%a2,%d0.w),%d0
        and.l   %d4,%d0
        beq.s   3f
        move.w  %d2,(%a1)
        dbra    %d3,2b
        bra.s   9f
3:      move.w  #0,(%a1)
        dbra    %d3,2b
        bra.s   9f
5:      moveq   #0,%d0                  | sin mascara (capa 1)
        move.b  (%a0)+,%d0
        cmpi.b  #0xFF,%d0
        beq.s   6f
        sub.w   %d5,%d0
        andi.w  #63,%d0
        cmp.w   %d6,%d0
        bcc.s   6f
        move.w  %d2,(%a1)
        dbra    %d3,5b
        bra.s   9f
6:      move.w  #0,(%a1)
        dbra    %d3,5b
9:      movem.l (%sp)+,%d2-%d6/%a2
        rts


| void himask_asm(u32 xs, u32 xa, u32 xz, u32 c0, u32 *himask): para las NCOL columnas de pantalla
| (columna c0+k en x = xs + 13k), himask[col] = c1mask[col] si xa < x < xz, si no 0
        .globl  himask_asm
himask_asm:
        movem.l %d2-%d5/%a2,-(%sp)
        move.w  26(%sp),%d0             | xs
        move.w  30(%sp),%d1             | xa
        move.w  34(%sp),%d2             | xz
        move.w  38(%sp),%d3             | c0
        movea.l 40(%sp),%a0             | himask
        lea     c1mask,%a1
        move.w  vconst+8,%d4
        subq.w  #1,%d4
1:      moveq   #63,%d5
        and.w   %d3,%d5
        add.w   %d5,%d5
        add.w   %d5,%d5
        cmp.w   %d1,%d0
        ble.s   2f
        cmp.w   %d2,%d0
        bge.s   2f
        move.l  (%a1,%d5.w),(%a0,%d5.w)
        bra.s   3f
2:      clr.l   (%a0,%d5.w)
3:      addq.w  #1,%d3
        addi.w  #13,%d0
        dbra    %d4,1b
        movem.l (%sp)+,%d2-%d5/%a2
        rts

| void spr_bbox(const u16 *rec, u32 n, s16 *out): rectangulo (x0, x1, y0, y1) de n registros
| (x de SCB4 sin ancho; y de pantalla de SCB3 y fondo y + 16*altura)
        .globl  spr_bbox
spr_bbox:
        movem.l %d2-%d7,-(%sp)
        movea.l 28(%sp),%a0
        move.l  32(%sp),%d7
        move.w  #32767,%d2              | x0
        move.w  #-32768,%d3             | x1
        move.w  #32767,%d4              | y0
        move.w  #-32768,%d5             | y1
        subq.w  #1,%d7
        bcs.s   9f
1:      move.w  (%a0)+,%d0              | SCB3
        moveq   #63,%d6
        and.w   %d0,%d6                 | altura
        lsr.w   #7,%d0
        move.w  #496,%d1
        sub.w   %d0,%d1
        andi.w  #511,%d1
        cmpi.w  #384,%d1
        blt.s   2f
        subi.w  #512,%d1
2:      cmp.w   %d4,%d1
        bge.s   3f
        move.w  %d1,%d4
3:      move.w  %d6,%d0
        lsl.w   #4,%d0
        add.w   %d1,%d0
        cmp.w   %d5,%d0
        ble.s   4f
        move.w  %d0,%d5
4:      move.w  (%a0)+,%d0              | SCB4
        lsr.w   #7,%d0
        cmpi.w  #448,%d0
        blt.s   5f
        subi.w  #512,%d0
5:      cmp.w   %d2,%d0
        bge.s   6f
        move.w  %d0,%d2
6:      cmp.w   %d3,%d0
        ble.s   7f
        move.w  %d0,%d3
7:      add.w   %d6,%d6
        add.w   %d6,%d6
        adda.w  %d6,%a0                 | saltar los tiles
        dbra    %d7,1b
9:      movea.l 36(%sp),%a0
        move.w  %d2,(%a0)+
        move.w  %d3,(%a0)+
        move.w  %d4,(%a0)+
        move.w  %d5,(%a0)
        movem.l (%sp)+,%d2-%d7
        rts

| u32 vis_bits(const u8 *tab, u32 c0, const u32 *mask, u32 win): bit s = la ranura s muestra su
| columna tab[s]: en pantalla ((col - c0) mod 64 < NCOL) y, con mascara, con tiles en la ventana
        .globl  vis_bits, scb3_bits
vis_bits:
        | recorre solo las NCOL columnas de la ventana: col = c0+k, ranura = col & 31
        movem.l %d2-%d6/%a2,-(%sp)
        movea.l 28(%sp),%a0             | tab (tcol de la capa)
        move.w  34(%sp),%d5             | c0
        movea.l 36(%sp),%a2             | mask (0 = sin mascara)
        move.l  40(%sp),%d4             | win
        move.w  vconst+8,%d6            | NCOL
        subq.w  #1,%d6
        moveq   #0,%d2
        move.l  %a2,%d0
        beq.s   5f
1:      moveq   #63,%d0
        and.w   %d5,%d0                 | col
        moveq   #31,%d1
        and.w   %d0,%d1                 | ranura
        cmp.b   (%a0,%d1.w),%d0
        bne.s   2f
        add.w   %d0,%d0
        add.w   %d0,%d0
        move.l  (%a2,%d0.w),%d3
        and.l   %d4,%d3
        beq.s   2f
        bset    %d1,%d2
2:      addq.w  #1,%d5
        dbra    %d6,1b
        bra.s   9f
5:      moveq   #63,%d0
        and.w   %d5,%d0
        moveq   #31,%d1
        and.w   %d0,%d1
        cmp.b   (%a0,%d1.w),%d0
        bne.s   6f
        bset    %d1,%d2
6:      addq.w  #1,%d5
        dbra    %d6,5b
9:      move.l  %d2,%d0
        movem.l (%sp)+,%d2-%d6/%a2
        rts
| void scb3_bits(u32 vaddr, u32 yv, u32 bits): SCB3 de 32 ranuras: yv donde el bit esta a 1, 0 si no
scb3_bits:
        move.l  %d2,-(%sp)
        lea     0x3C0002,%a1
        move.w  #1,2(%a1)
        move.w  10(%sp),-2(%a1)
        move.w  14(%sp),%d0
        move.l  16(%sp),%d1
        moveq   #31,%d2
1:      lsr.l   #1,%d1
        bcc.s   2f
        move.w  %d0,(%a1)
        dbra    %d2,1b
        bra.s   9f
2:      move.w  #0,(%a1)
        dbra    %d2,1b
9:      move.l  (%sp)+,%d2
        rts

| ---------------------------------------------------------------------------------------------
| void l1_build(void): sustituye al constructor de la lista 1 del juego (0x457C, en su IRQ):
| objetos del decorado (publico, arboles, vallas...). En vez de escribir la lista del juego y
| convertirla luego entrada a entrada (spr_add), escribe directamente los registros de sprites
| Neo Geo (cabecera SCB3|altura, SCB4, y tile/atributo por fila) en un buffer intermedio
| (clase 0 en ST_BUF0, clase 1 en ST_BUF1). Mismo dibujo que el original + spr_add; ademas los
| huecos (tile 0) dentro de una columna se rellenan con el tile vacio en vez de partirla.
| (Los objetos cambian de indice casi en cada cuadro -el publico esta animado-, asi que no
| compensa guardar la disposicion de un cuadro para el siguiente.)
| Filas: el original admite -16 <= y < 240 y spr_ytab deja ver -3 <= y <= 235, donde
| SCB3 = ((276 + y) & 511) << 7 y baja 0x800 por fila: se recorren solo esas filas.
| Con la pantalla girada (FEF824) se usa el codigo original y su lista (st_ok = 0).
| Registros: d0 x columna, d3 filas, d4 descriptor, d5 SCB3 de la fila, d6 desc0 en cache,
| d7 altura del registro abierto (-1 = ninguno), a0 objeto, a1 escritura clase 0, a2 columna,
| a3 desplazamiento:atributo en cache, a4 cabecera abierta, a5 descriptor de la fila, a6 paso
        .globl  l1_build, st_ok, st_n0, st_n1, st_w0, st_w1, st_bb, lcopyl
        .equ    ST_BUF0, 0x109800
        .equ    ST_END0, 0x109800 + 688 - 8   | (cabe en rec0 con margen para las otras listas)
        | clase 1 (haces de los faros: hasta 84 tiles por cuadro en las etapas de noche; medido como
        | mucho 408 bytes) en la cola sin usar del banco 7 de paletas del juego (paletas 48-63). Antes
        | estaba en la RAM de paletas del Neo Geo: en hardware real eso pinta puntos y rayas
        .equ    ST_BUF1, 0x107E00
        .equ    ST_END1, 0x107E00 + 512 - 8
l1_build:
        tst.w   0x10F824
        beq.s   1f
        move.b  #0,st_ok
        lea     0x10C58C,%a0
        tst.w   0x10C28A
        beq.s   0f
        lea     0x10C28C,%a0
0:      jmp     0x4590
        | una sola vez por cuadro del juego: si el bucle principal no ha terminado otro cuadro desde
        | la ultima (la IRQ ha llegado a mitad de un cuadro largo), se conservan los registros de
        | entonces (el estado esta a medias) y se ahorra el trabajo justo cuando falta tiempo
1:      tst.b   l1_need
        bne.s   3f
        cmpi.b  #4,l1_age
        bcc.s   3f                      | (bucle principal sin frame_convert: siempre)
        move.l  #0x0000FFFF,%d2         | d2 como el original al salir (ver abajo)
        rts
3:      move.b  #0,l1_need
        .ifdef PROFB
        move.w  #1,0x10BFF6
        .endif
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        lea     0x10C58C,%a0
        tst.w   0x10C28A
        beq.s   2f
        lea     0x10C28C,%a0
2:      lea     ST_BUF0,%a1
        move.l  #ST_BUF1,st_q1
        move.l  #0x7FFF8000,st_bb       | x0, x1 (rectangulo de los registros de clase 0)
        move.l  #0x7FFF8000,st_bb+4     | y0, y1
        move.w  #0,st_n0
        move.w  #0,st_n1
        move.w  eff_np,eff_npl          | (haces del cuadro anterior)
        clr.w   eff_np
        move.w  #1,st_y1n               | (ninguna fila coincide: bit 0 nunca esta en un SCB3 de fila)
        moveq   #-1,%d6                 | desc0 en cache: ninguno
| ---- objetos: (indice, X, Y); fin con el bit 15 ----
.Lob:   move.w  (%a0),%d0
        bmi     .Lend
        moveq   #0,%d1
        move.w  %d0,%d1
        lsl.l   #2,%d1
        lea     0xD07D6,%a5
        adda.l  %d1,%a5
        moveq   #0,%d1
        move.w  (%a5)+,%d1
        movea.l 0x10D5FC,%a2
        adda.l  %d1,%a2                 | descriptores (desc0, desc1) por tile
        move.w  2(%a0),%d0
        subi.w  #69,%d0
        sub.w   0x10C244,%d0            | x de la primera columna
        move.w  4(%a0),%d1
        addi.w  #13,%d1
        move.w  0x10C252,%d5
        addi.w  #272,%d5
        sub.w   %d1,%d5                 | y de la fila 0 (baja 16 por fila)
        move.w  (%a5),%d2
        moveq   #0,%d3
        move.b  %d2,%d3                 | filas
        lsr.w   #8,%d2                  | columnas
        | objeto entero fuera por los lados (x de columna fuera de 0..404: ni el original ni
        | spr_xtab lo dejan ver): fuera sin mas
        cmpi.w  #405,%d0
        bge     .Lnob
        move.w  %d2,%d1
        lsl.w   #4,%d1
        add.w   %d0,%d1
        bmi     .Lnob
        move.w  %d2,%d1
        add.w   %d1,%d1
        add.w   %d1,%d1
        movea.w %d1,%a6                 | paso entre filas
        | filas con -3 <= y <= 235
        ext.l   %d5
        moveq   #0,%d4                  | primera fila
        move.l  %d5,%d1
        subi.l  #235,%d1
        ble.s   3f
        addi.l  #15,%d1
        asr.l   #4,%d1                  | techo((y0 - 235) / 16)
        move.l  %d1,%d4
3:      move.l  %d5,%d7
        addq.l  #3,%d7
        bmi     .Lnob                   | todo por debajo de la pantalla
        asr.l   #4,%d7                  | ultima fila posible
        subq.w  #1,%d3
        cmp.l   %d3,%d7
        ble.s   4f
        move.l  %d3,%d7
4:      sub.l   %d4,%d7                 | filas a recorrer - 1
        bmi     .Lnob
        move.w  %d7,st_rows
        move.w  %d4,%d1
        lsl.w   #4,%d1
        sub.w   %d1,%d5                 | y de la primera fila
        move.w  #220,%d1
        sub.w   %d5,%d1
        move.w  %d1,st_ytop             | rectangulo del objeto en pantalla: arriba
        lsl.w   #4,%d7
        add.w   %d7,%d1
        addi.w  #16,%d1
        move.w  %d1,st_ybot             | y abajo
        addi.w  #276,%d5
        andi.w  #511,%d5
        lsl.w   #7,%d5
        move.w  %d5,st_y0               | SCB3 (sin altura) de la primera fila
        move.w  %a6,%d1
        mulu.w  %d4,%d1
        adda.l  %d1,%a2                 | descriptor de la primera fila
        move.l  %a1,st_a1o              | (para saber si el objeto ha dejado algo)
        subq.w  #1,%d2
        bcs     .Lnob
        move.w  %d2,st_cols
| ---- columnas ----
.Lcol:  cmpi.w  #512,%d0
        bcc     .Lncol
        move.w  %d0,%d2
        add.w   %d2,%d2
        lea     spr_xtab,%a4
        move.w  (%a4,%d2.w),%d2         | SCB4 de la columna
        btst    #0,%d2
        bne     .Lncol                  | fuera de pantalla
        move.w  %d2,st_sx0
        move.w  %d2,st_sx               | (vale para el desc0 en cache si sus bits 0-1 son 0)
        moveq   #3,%d1
        and.w   %d6,%d1
        beq.s   1f
        moveq   #-1,%d6                 | si no, recalcular
1:      move.l  %a1,st_a1c
        movea.l %a2,%a5
        move.w  st_y0,%d5
        move.w  st_rows,%d3
        moveq   #-1,%d7
| ---- filas ----
.Lrow:  move.l  (%a5),%d4               | desc0:desc1
        tst.w   %d4
        beq     .Lgap
.Lrow2: swap    %d4
        cmp.w   %d6,%d4
        bne     .Lcalc
.Lhave: swap    %d4
        addi.w  #0x2E00,%d4
        andi.w  #0x3FFF,%d4             | tile del juego
        cmpi.w  #0x3700,%d4
        bcc     .Lcls1
        move.l  %a3,%d1                 | desplazamiento : atributo
        swap    %d1
        add.w   %d4,%d1                 | atributo : tile final
        tst.w   %d7
        bmi     .Lnew
        cmpi.w  #31,%d7
        bcc     .Lnew
        addq.w  #1,%d7
        addq.w  #1,(%a4)                | altura + 1
        move.w  %d1,(%a1)+
        swap    %d1
        move.w  %d1,(%a1)+
.Lnrow: subi.w  #0x800,%d5
        adda.w  %a6,%a5
        dbra    %d3,.Lrow
.Lecol: cmpa.l  st_a1c,%a1              | la columna ha dejado algo: x en el rectangulo
        beq.s   .Lncol
        move.w  st_sx0,%d2
        lsr.w   #7,%d2
        cmpi.w  #448,%d2
        blt.s   5f
        subi.w  #512,%d2
5:      cmp.w   st_bb,%d2
        bge.s   6f
        move.w  %d2,st_bb
6:      cmp.w   st_bb+2,%d2
        ble.s   .Lncol
        move.w  %d2,st_bb+2
.Lncol: addq.l  #4,%a2
        addi.w  #16,%d0
        subq.w  #1,st_cols
        bcc     .Lcol
        cmpa.l  st_a1o,%a1              | el objeto ha dejado algo: y en el rectangulo
        beq.s   .Lnob
        move.w  st_ytop,%d2
        cmp.w   st_bb+4,%d2
        bge.s   6f
        move.w  %d2,st_bb+4
6:      move.w  st_ybot,%d2
        cmp.w   st_bb+6,%d2
        ble.s   .Lnob
        move.w  %d2,st_bb+6
.Lnob:  addq.l  #6,%a0
        bra     .Lob
| hueco (tile 0): si hay registro abierto se cuentan los huecos seguidos y, si luego viene otro
| tile en la columna y cabe, se rellenan con el tile vacio
.Lgap:  tst.w   %d7
        bmi     .Lnrow
        moveq   #1,%d2
1:      subi.w  #0x800,%d5
        adda.w  %a6,%a5
        subq.w  #1,%d3
        bmi.s   8f                      | fin de columna: el registro queda como estaba
        move.l  (%a5),%d4
        tst.w   %d4
        bne.s   2f
        addq.w  #1,%d2
        bra.s   1b
2:      move.w  %d7,%d1
        add.w   %d2,%d1
        cmpi.w  #31,%d1
        bcc.s   7f                      | no cabe: se corta
        move.w  %d1,%d7
        add.w   %d2,(%a4)
        move.w  vconst+6,%d1            | tile vacio, atributo 0
        subq.w  #1,%d2
3:      move.w  %d1,(%a1)+
        move.w  #0,(%a1)+
        dbra    %d2,3b
        bra     .Lrow2
7:      moveq   #-1,%d7
        bra     .Lrow2
8:      moveq   #-1,%d7
        bra     .Lecol
| registro nuevo (clase 0)
.Lnew:  btst    #0,st_sx+1              | x fuera de pantalla
        bne.s   .Lskip
        cmpa.l  #ST_END0,%a1
        .ifdef CAPCNT
        bcs.s   93f
        addq.w  #1,0x10BFE4
        bra     .Lskip
93:
        .else
        bcc     .Lcls1                  | clase 0 llena: con los de clase 1 (ver spr_add, .Cov0)
        .endif
        move.w  st_n0,%d2
        cmp.w   vconst+12,%d2
        .ifdef CAPCNT
        bcs.s   94f
        addq.w  #1,0x10BFE6
        bra     .Lskip
94:
        .else
        bcc     .Lcls1
        .endif
        addq.w  #1,st_n0
        movea.l %a1,%a4
        move.w  %d5,%d2
        addq.w  #1,%d2                  | altura 1
        move.w  %d2,(%a1)+
        move.w  st_sx,%d2
        move.w  %d2,st_kx
        move.w  %d2,(%a1)+
        move.w  %d1,(%a1)+
        swap    %d1
        move.w  %d1,(%a1)+
        moveq   #1,%d7
        bra     .Lnrow
.Lskip: moveq   #-1,%d7
        bra     .Lnrow
| desc0 distinto: atributo (tabla por byte bajo + volteo) y SCB4 (los bits 0-1 del desc0 entran
| en la palabra x del juego). El registro abierto sigue si la x no cambia
.Lcalc: move.w  %d4,%d6
        moveq   #0,%d2
        move.b  %d4,%d2
        add.w   %d2,%d2
        add.w   %d2,%d2
        move.l  %a4,-(%sp)
        lea     l1_attab,%a4
        move.l  (%a4,%d2.w),%d2         | desplazamiento : atributo
        move.w  %d4,%d1
        rol.w   #2,%d1
        andi.w  #3,%d1                  | volteo
        or.w    %d1,%d2
        movea.l %d2,%a3
        moveq   #3,%d1
        and.w   %d4,%d1
        bne.s   1f
        move.w  st_sx0,%d1
        bra.s   2f
1:      lsl.w   #8,%d1
        or.w    %d0,%d1
        andi.w  #0x3FF,%d1
        add.w   %d1,%d1
        lea     spr_xtab,%a4
        move.w  (%a4,%d1.w),%d1
2:      movea.l (%sp)+,%a4
        move.w  %d1,st_sx
        tst.w   %d7
        bmi     .Lhave
        cmp.w   st_kx,%d1
        beq     .Lhave
        moveq   #-1,%d7                 | otra x: se corta
        bra     .Lhave
| tile de clase 1: se agrupan en columnas como los de clase 0 (misma x, filas seguidas). Antes iba
| un registro (un sprite) por tile y los haces de los faros (hasta 84 tiles) no cabian: se perdian
.Lcls1: moveq   #-1,%d7
        btst    #0,st_sx+1
        bne     .Lnrow
        move.l  %a3,%d1
        swap    %d1
        add.w   %d4,%d1                 | atributo : tile final
        | tile de efecto (paleta PAL_SH = 64: haces de los faros): se apunta su posicion para que
        | effect_bank (video.c) mire el fondo que hay debajo del haz
        move.l  %d1,%d2
        swap    %d2
        lsr.w   #8,%d2
        cmpi.w  #64,%d2
        bne.s   85f
        move.w  eff_np,%d2
        addq.w  #1,eff_np
        andi.w  #7,%d2
        add.w   %d2,%d2
        lea     eff_px,%a4
        move.w  st_sx,(%a4,%d2.w)
        lea     eff_py,%a4
        move.w  %d5,(%a4,%d2.w)
85:     movea.l st_q1,%a4
        cmpa.l  #ST_END1,%a4
        .ifdef CAPCNT
        bcs.s   96f
        addq.w  #1,0x10BFEA
        bra     .Lnrow
96:
        .else
        bcc     .Lnrow
        .endif
        | sigue el registro abierto: misma x, fila siguiente y cabe
        cmp.w   st_y1n,%d5
        bne.s   1f
        move.w  st_sx,%d2
        cmp.w   st_kx1,%d2
        bne.s   1f
        cmpi.w  #31,st_c1
        bcc.s   1f
        move.w  %d1,(%a4)+
        swap    %d1
        move.w  %d1,(%a4)+
        move.l  %a4,st_q1
        movea.l st_h1p,%a4
        addq.w  #1,(%a4)                | altura + 1
        addq.w  #1,st_c1
        move.w  %d5,%d2
        subi.w  #0x800,%d2
        move.w  %d2,st_y1n
        bra     .Lnrow
1:      move.w  st_n1,%d2               | registro nuevo
        cmp.w   vconst+14,%d2
        .ifdef CAPCNT
        bcs.s   97f
        addq.w  #1,0x10BFEC
        bra     .Lnrow
97:
        .else
        bcc     .Lnrow
        .endif
        addq.w  #1,st_n1
        move.l  %a4,st_h1p
        move.w  %d5,%d2
        addq.w  #1,%d2
        move.w  %d2,(%a4)+
        move.w  st_sx,%d2
        move.w  %d2,st_kx1
        move.w  %d2,(%a4)+
        move.w  %d1,(%a4)+
        swap    %d1
        move.w  %d1,(%a4)+
        move.l  %a4,st_q1
        move.w  #1,st_c1
        move.w  %d5,%d2
        subi.w  #0x800,%d2
        move.w  %d2,st_y1n
        bra     .Lnrow
.Lend:  move.l  %a1,%d0
        subi.l  #ST_BUF0,%d0
        lsr.w   #1,%d0
        move.w  %d0,st_w0
        move.l  st_q1,%d1
        subi.l  #ST_BUF1,%d1
        lsr.w   #1,%d1
        move.w  %d1,st_w1
        add.w   %d1,%d0                 | palabras: 2 por tile + 2 por registro
        lsr.w   #1,%d0
        sub.w   st_n0,%d0
        sub.w   st_n1,%d0
        move.w  %d0,0x10C27E            | cuenta de tiles (como el original)
        move.b  #1,st_ok
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        | el original sale con d2 = 0x0000FFFF (clr.l d2 + dbra) y la IRQ del juego lo usa luego:
        | divu.w #6 tras un move.w (0x30D0, puntos cada 6 ticks). Con la mitad alta distinta se
        | sumaban puntos de mas
        move.l  #0x0000FFFF,%d2
        .ifdef PROFB
        move.w  #2,0x10BFF6
        .endif
        rts
        .bss
        .even
st_q1:  .skip 4
st_h1p: .skip 4
st_y1n: .skip 2
st_kx1: .skip 2
st_c1:  .skip 2
st_a1c: .skip 4
st_a1o: .skip 4
st_bb:  .skip 8
st_n0:  .skip 2
st_n1:  .skip 2
st_w0:  .skip 2
st_w1:  .skip 2
st_y0:  .skip 2
st_ytop: .skip 2
st_ybot: .skip 2
st_rows: .skip 2
st_cols: .skip 2
st_sx:  .skip 2
st_sx0: .skip 2
st_kx:  .skip 2
st_ok:  .skip 1
        .globl  l1_need, l1_age
l1_need: .skip 1
l1_age: .skip 1
        .text

| void lcopyl(void *d, const void *s, u32 nl): copia nl longs (de 4 en 4)
lcopyl: movea.l 4(%sp),%a1
        movea.l 8(%sp),%a0
        move.l  12(%sp),%d0
        move.w  %d0,%d1
        andi.w  #3,%d1
        lsr.w   #2,%d0
        bra.s   2f
1:      move.l  (%a0)+,(%a1)+
        move.l  (%a0)+,(%a1)+
        move.l  (%a0)+,(%a1)+
        move.l  (%a0)+,(%a1)+
2:      dbra    %d0,1b
        bra.s   4f
3:      move.l  (%a0)+,(%a1)+
4:      dbra    %d1,3b
        rts

| busqueda del juego en 0xA07A: a1 = tabla (registros: clave, datos..., 0xFF), d0 = clave.
| Con indice precalculado (srch_idx) para las cuatro tablas y claves 0..63; si no, como el
| original. Sigue en 0xA08E con a1 justo detras de la clave encontrada
        .globl  srch_hook
srch_hook:
        | d1 y a0 son del juego (a0 se usa en 0xA0A0; d1 llega al llamador si la lista esta vacia):
        | se conservan los dos
        movem.l %d1/%a0,-(%sp)
        move.l  %a1,%d1
        lea     srch_idx,%a0
        cmpi.l  #0xE15B2,%d1
        beq.s   1f
        lea     256(%a0),%a0
        cmpi.l  #0xE1A12,%d1
        beq.s   1f
        lea     256(%a0),%a0
        cmpi.l  #0xE1382,%d1
        beq.s   1f
        lea     256(%a0),%a0
        cmpi.l  #0xE17E2,%d1
        bne.s   5f
1:      cmpi.w  #64,%d0
        bcc.s   5f
        move.w  %d0,%d1
        add.w   %d1,%d1
        add.w   %d1,%d1
        move.l  (%a0,%d1.w),%d1
        beq.s   5f
        .ifdef SRCHCHK
        move.l  %a1,-(%sp)
7:      cmp.w   (%a1),%d0
        beq.s   8f
9:      addq.l  #2,%a1
        cmpi.w  #0xFF,(%a1)
        bne.s   9b
        addq.l  #2,%a1
        bra.s   7b
8:      cmp.l   %a1,%d1
        beq.s   3f
        addq.w  #1,0x10BFE0             | (diferencia)
3:      movea.l (%sp)+,%a1
        addq.w  #1,0x10BFE2
        .endif
        movea.l %d1,%a1
        bra.s   6f
5:      cmp.w   (%a1),%d0               | como el original
        beq.s   6f
4:      addq.l  #2,%a1
        cmpi.w  #0xFF,(%a1)
        bne.s   4b
        addq.l  #2,%a1
        bra.s   5b
6:      addq.l  #2,%a1
        movem.l (%sp)+,%d1/%a0
        jmp     0xA08E

| ---------------------------------------------------------------------------------------------
| recorrido del juego (0x9E00, cada 12 cuadros): 15 filas x 24 celdas de la capa 0 en pantalla
| buscando tiles animados ((t - 0x1A00) & 0x1F80 = 0x0E00 o 0x1080, es decir t & 0x1F80 = 0x0800
| o 0x0A80) que cambia por los de las tablas 0xF01FC / 0xF05FC. Mismo resultado que el original
| (registros incluidos) pero en ~1/3 del tiempo: el original eran ~48000 ciclos de golpe en un
| solo cuadro, y ese cuadro no llegaba al VBLANK
        .globl  scan_hook
scan_hook:
        movea.l #0x100000,%a0
        movem.l %d6-%d7/%a2-%a3,-(%sp)
        move.w  #0x1F80,%d6
        move.w  #0x0800,%d7
        movea.w #0x0A80,%a3
        move.w  #14,%d3
1:      move.w  #23,%d2
2:      move.w  (%a0,%d0.w),%d4
        and.w   %d6,%d4
        cmp.w   %d7,%d4
        beq.s   5f
        cmp.w   %a3,%d4
        beq.s   6f
3:      addq.b  #4,%d0
        dbf     %d2,2b
        subi.b  #96,%d0
        addi.w  #256,%d0
        andi.w  #0x1FFF,%d0
        dbf     %d3,1b
        | como el original al salir: d4 y a1 de la ultima celda
        subi.w  #0x1A00,%d4
        andi.w  #0x1F80,%d4
        lea     0xF01FC,%a1
        cmpi.w  #0x0E00,%d4
        beq.s   4f
        lea     0xF05FC,%a1
4:      movem.l (%sp)+,%d6-%d7/%a2-%a3
        jmp     0x9E92
5:      lea     0xF01FC,%a1
        bra.s   7f
6:      lea     0xF05FC,%a1
7:      move.l  (%a0,%d0.w),%d1
        btst    #6,%d1
        bne.s   8f
        swap    %d1
        andi.w  #127,%d1
        cmpi.w  #90,%d1
        bge     3b
        add.w   %d1,%d1
        add.w   %d1,%d1
        jsr     scan_hit                | (escritura de VRAM del juego, ya descifrada)
        bra     3b
8:      swap    %d1
        andi.w  #127,%d1
        cmpi.w  #90,%d1
        bge     3b
        bset    #7,%d1
        add.w   %d1,%d1
        add.w   %d1,%d1
        jsr     scan_hit
        bra     3b

| tras el fundido de paletas del juego (0xB01E): paletas Neo Geo ya, y la instruccion reemplazada
| (move.w $FEEA3C,d0: direccion fija en el port)
        .globl  palb_hook
palb_hook:
        move.b  #1,pal_evt
        movem.l %d1/%a0-%a1,-(%sp)
        | el fundido del juego va dentro de su IRQ (VBLANK enmascarado) y la conversion son hasta
        | ~40000 ciclos: con el VBLANK cortado el siguiente llegaba 15 lineas tarde (sprites y paletas
        | con la imagen ya empezada). Se abre: un VBLANK anidado solo hace lo nuestro (chain_busy)
        move.w  %sr,-(%sp)
        andi.w  #0xF8FF,%sr
        lea     pal_sync_now,%a1
        jsr     (%a1)
        move.w  (%sp)+,%sr
        movem.l (%sp)+,%d1/%a0-%a1
        move.w  0x10EA3C,%d0
        rts

| trampolines tras las cargas de paletas enteras: paletas Neo Geo ya y luego la instruccion
| desplazada (build.py la copia sobre los NOP, a 14 bytes del inicio)
        .globl  palt6, palt10, palt_hud
palt6:  move.b  #1,pal_force              | 8 (al terminar el cuadro: frame_convert)
        nop                             | 2
        nop
        nop
        nop                             | 2
        nop                             | 2
        nop
        nop
        nop
        rts
palt10: move.b  #1,pal_force              | 8 (al terminar el cuadro: frame_convert)
        nop                             | 2
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        rts
| 0x124D6 "dbra d0,$124D2 / rts" (paletas del marcador)
palt_hud:
        dbra    %d0,1f
        move.b  #1,pal_force
        rts
1:      jmp     0x124D2

