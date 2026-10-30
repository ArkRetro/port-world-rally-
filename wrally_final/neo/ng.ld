OUTPUT_ARCH(m68k)
ENTRY(user_entry)
MEMORY { p1 (rx) : ORIGIN = 0, LENGTH = 1M
         p2 (rx) : ORIGIN = 0x200000, LENGTH = 1M
         ram (rwx) : ORIGIN = 0x10A000, LENGTH = 0x2000 }
SECTIONS {
  .vec : { KEEP(*(.text.vectors)) . = 0x100; KEEP(*(.text.header)) } > p1
  .text : { *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); } > p2
  .data : { _data_start = .; *(.data .data.*) . = ALIGN(4); _data_end = .; } > ram AT > p2
  _data_load = LOADADDR(.data);
  .bss (NOLOAD) : { _bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > ram
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
  ASSERT(_bss_end <= 0x10BF00, "bss invade la zona de depuracion 0x10BF00")
}
