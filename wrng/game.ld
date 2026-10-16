/* game.ld - runtime image for the game build: vectors + header at 0, code at RUNTIME_ORG (tools/mkrom.py) */
OUTPUT_ARCH(m68k)
ENTRY(_start)
SECTIONS
{
    . = 0x000000;
    .vectors : { *(.vectors) }
    . = 0x015000;
    .text : {
        *(.text)
        *(.rodata*)
        *(.data)
    }
    /DISCARD/ : { *(.comment) *(.note*) }
}
