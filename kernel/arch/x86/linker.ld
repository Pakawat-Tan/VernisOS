OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS {
    . = 0x100000;    /* Load at 1MB */

    .text ALIGN(4K) : {
        _text_start = .;
        *(.text.entry)   /* Entry point code first */
        *(.text)         /* Then regular code */
        *(.text.*)
        _text_end = .;
    }

    .rodata ALIGN(4K) : {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    }

    .data ALIGN(4K) : {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    }

    .bss ALIGN(4K) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
    }
}
