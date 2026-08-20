ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text ALIGN(0x1000) : {
        *(.text .text.*)
    }

    .rodata ALIGN(0x1000) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(0x1000) : {
        *(.data .data.*)
    }

    .bss ALIGN(0x1000) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
