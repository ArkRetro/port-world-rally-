    # EXE de tamanos R1 (solo se enlaza, para medir el area): sin la ROM contigua de R0
    .section .rodata
    .balign 4
    .globl mcu_tab
mcu_tab: .incbin "mcu_tab.bin"
    .data
    .balign 4
    .globl rc_rom
rc_rom: .space 4
