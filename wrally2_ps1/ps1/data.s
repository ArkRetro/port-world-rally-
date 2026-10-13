    .section .rodata
    .balign 4
    .globl mcu_rom
mcu_rom: .incbin "mcu.bin"
    .data
    .balign 4
    .globl rc_rom
rc_rom: .incbin "rom_sw.bin"
    .space 4
