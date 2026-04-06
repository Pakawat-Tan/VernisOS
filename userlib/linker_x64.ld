/* VernisOS user linker script — x86_64 */
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
