OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
    . = 0x100000;    /* Match bootloader's loading address */

    .text ALIGN(4K) : {
        *(.text.entry)   /* Entry point code first */
        *(.text)
        *(.text.*)
        . = ALIGN(4K);
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4K);
    }

    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
        *(.got)
        *(.got.plt)
        . = ALIGN(4K);
    }

    .bss ALIGN(4K) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        . = ALIGN(4K);
        _bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
    }
}