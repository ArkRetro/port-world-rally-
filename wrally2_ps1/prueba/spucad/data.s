# Datos de la sonda spucad: bloque de silencio y trozos PS-ADPCM (gen.py); se suben a 0x1000 de la RAM de la SPU.
    .section .rodata
    .align 4
    .global tz_datos
tz_datos:
    .incbin "trozos.bin"
