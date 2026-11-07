    .section .rodata
    .balign 4
    .globl mcu_tab
mcu_tab: .incbin "mcu_tab.bin"
    .data
    .balign 4
    .globl rc_rom
rc_rom: .incbin "rom_sw.bin"
    .space 4
