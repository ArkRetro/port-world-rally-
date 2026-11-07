# Arranque del menu: guarda como lo dejo la BIOS el estado de las interrupciones (SR, I_MASK) y de la DMA (DPCR), que
# menu.c devuelve tal cual antes de LoadExec (la BIOS lee el EXE del juego con su controlador de CD, que va por
# interrupciones), y luego como el crt0 del WR1: sin interrupciones, la pila en su sitio y la BSS a cero.
    .section .start, "ax", @progbits
    .set noreorder
    .align 2
    .global _start
    .type _start, @function
_start:
    mfc0  $s0, $12              # SR de la BIOS
    lui   $t0, 0xBF80
    lw    $s1, 0x1074($t0)      # I_MASK
    lw    $s2, 0x10F0($t0)      # DPCR
    mtc0  $0, $12               # SR = 0: sin interrupciones
    sw    $0, 0x1074($t0)       # I_MASK = 0 (I_STAT no se toca: lo pendiente es de la BIOS)
    lui   $t1, 0x801F
    ori   $t1, $t1, 0xFFF0
    move  $sp, $t1              # pila como STACK de SYSTEM.CNF
    la    $t0, __bss_start
    la    $t1, __bss_end
    beq   $t0, $t1, 2f
    nop
1:  sw    $0, 0($t0)
    addiu $t0, 4
    bne   $t0, $t1, 1b
    nop
2:  move  $a0, $s0
    move  $a1, $s1
    j     menu
    move  $a2, $s2

# LoadExec(nombre, pila, desplazamiento) de la BIOS (A(51h)): carga el EXE y salta a el; solo vuelve si falla
    .text
    .global bios_loadexec
    .type bios_loadexec, @function
bios_loadexec:
    li    $t2, 0xA0
    jr    $t2
    li    $t1, 0x51

# devuelve SR (las interrupciones como estaban) justo antes de llamar a la BIOS
    .global pon_sr
    .type pon_sr, @function
pon_sr:
    jr    $ra
    mtc0  $a0, $12
