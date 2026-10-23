/* game.ld - runtime image: vectors + header at 0, code at RUNTIME_ORG (tools/mkrom.py), tables in P2 */
OUTPUT_ARCH(m68k)
ENTRY(_start)
SECTIONS
{
    . = 0x000000;
    .vectors : { *(.vectors) }
    __vectors_end = .;
    . = 0x0B0000;
    __text_start = .;
    .text : {
        *(.text)
        *(.rodata*)
        *(.data)
    }
    __text_end = .;
    . = 0x200000;
    .p2 : { *(.p2) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
