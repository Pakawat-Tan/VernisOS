/* VernisOS user linker script — i686 */
ENTRY(_start)

SECTIONS
{
    . = 0x10000000;

    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
