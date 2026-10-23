# la pantalla del menu (pantalla.py: cabecera de 16 halfwords, la pantalla y el rotulo de PULSA START)
    .section .rodata
    .align 4
    .global pantalla
pantalla:
    .incbin "pantalla.bin"
