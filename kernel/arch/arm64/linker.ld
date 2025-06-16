ENTRY(_start)

MEMORY {
    RAM : ORIGIN = 0x40000000, LENGTH = 128M
}

SECTIONS {
    . = 0x40000000;
    
    .text : {
        *(.text)
        *(.text.*)
    } > RAM
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > RAM
    
    .data : {
        *(.data)
        *(.data.*)
    } > RAM
    
    .bss : {
        bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        bss_end = .;
    } > RAM
    
    . = ALIGN(8);
    . = . + 0x1000; /* 4KB stack */
    stack_top = .;
}