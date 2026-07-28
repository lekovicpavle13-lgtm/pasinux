/* Freestanding kernel layout. Boot loads this image at physical 0x10000. */
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rdata : {
        *(.rdata)
        *(.rdata.*)
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.reloc)
        *(.idata*)
        *(.CRT*)
        *(.tls*)
        *(.rdata$zzz)
        *(.pdata)
        *(.xdata)
        *(.eh_frame*)
    }
}
