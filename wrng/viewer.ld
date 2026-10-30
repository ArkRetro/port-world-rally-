/* viewer.ld - everything in P1 from address 0 */
OUTPUT_ARCH(m68k)
ENTRY(_start)
SECTIONS
{
    . = 0x000000;
    .text : {
        *(.vectors)
        *(.text)
        *(.rodata*)
        *(.data)
    }
    /DISCARD/ : { *(.comment) *(.note*) }
}
