# Arranque del EXE (en lugar del crt0 de nugget): sin interrupciones y con la pila en su sitio antes de tocar nada.
# Un cargador de ODE (LoadExec del menu de PSIO, Fast Load de XStation) puede saltar aqui con las interrupciones
# activas y la pila en cualquier parte: al borrar la BSS (unos 450 KB) una interrupcion podria ir a un manejador del
# cargador que ya no esta, o la pila caer encima de la BSS. No se toca el kernel ni los vectores.
    .section .start, "ax", @progbits
    .set noreorder
    .align 2
    .global _start
    .type _start, @function
_start:
    mtc0  $0, $12               # SR = 0: sin interrupciones
    lui   $t0, 0xBF80
    sw    $0, 0x1074($t0)       # I_MASK = 0
    sw    $0, 0x1070($t0)       # I_STAT: reconocer lo pendiente
    lui   $t1, 0x801F
    ori   $t1, $t1, 0xFFF0
    move  $sp, $t1              # pila como STACK de SYSTEM.CNF (de una vez)
    la    $t0, __bss_start
    la    $t1, __bss_end
    beq   $t0, $t1, 2f
    nop
1:  sw    $0, 0($t0)
    addiu $t0, 4
    bne   $t0, $t1, 1b
    nop
2:  j     main
    nop
