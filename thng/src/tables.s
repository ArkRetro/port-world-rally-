* tables.s - P2 ROM (bank 0 at 0x200000): decryption + palette tables (tools/mkcrypt.py) and the 8x8 sprite
* tile map (tools/gfx2crom.py).  Addresses in ram.i (CT_*).
        .section .p2,"a"
        .incbin "crypt.bin"
        .incbin "map8.bin"
