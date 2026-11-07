# la pantalla del menu (pantalla.py: cabecera de 16 halfwords, la pantalla y el rotulo de PULSA START)
    .section .rodata
    .align 4
    .global pantalla
pantalla:
    .incbin "pantalla.bin"
# la musica (musica.py; vacia, la cabecera a cero, si se construye con SIN_MUSICA=1)
    .align 4
    .global musica
musica:
    .incbin "musica.bin"
